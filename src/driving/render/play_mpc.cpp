// Os filmes das missões de carro (driving/render/play_mpc.h; docs/driving.md seção 28).
#include "driving/render/play_mpc.h"

#include <cstring>
#include <deque>

#include "driving/game/events.h"
#include "driving/game/feedback.h"
#include "driving/game/mission.h"
#include "driving/game/triggers.h"
#include "driving/physics/simulation.h"
#include "driving/render/subtitles.h"
#include "driving/sound/audio_internal.h"
#include "driving/sound/audio_pause.h"
#include "driving/sound/snd.h"
#include "driving/sound/snd_stream.h"
#include "driving/sound/stream.h"
#include "platform/movie_decoder.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

MovieHost g_host;
MovieStats g_stats;

// A tabela dos quadros por segundo da libmpeg (0x003570b0, pelo frame_rate_code; o 1 é 22,977 no PS2).
constexpr uint32_t kFrameRates[9] = {0x00000000u, 0x41b7d0f1u, 0x41c00000u, 0x41c80000u, 0x41efc29fu,
                                     0x41f00000u, 0x42480000u, 0x426fc29fu, 0x42700000u};

uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// O lado do port do RCMP::DECODER e do PS2_SONY_CODEC_INTERNAL: o vídeo pelo decodificador do port, um quadro por vez.
struct Decoder {
    const MpcFile* file = nullptr;
    nf::Mpeg2Decoder dec;
    size_t next_unit = 0;
    bool drained = false;
    uint32_t count = 0;           // o +0x10 do codec: as imagens decodificadas
    std::vector<uint8_t> bgra;    // a imagem corrente (sceMpegGetPicture + a CSC do IPU)
    nf::MovieFrameNV12 nv12;

    bool open(const MpcFile& f) {
        file = &f;
        return dec.open(f.width, f.height, double(f.frame_rate), nf::MovieDecoderChoice::Own);
    }
    // sceMpegGetDecodeMode != 0: o fluxo acabou (o mpegNodata).
    bool finished() {
        while (dec.frames_ready() == 0) {
            if (next_unit < file->video.size()) {
                const MpcFile::Unit& u = file->video[next_unit++];
                dec.feed(file->data.data() + u.offset, u.size);
            } else if (!drained) {
                dec.drain();
                drained = true;
            } else {
                return true;
            }
        }
        return false;
    }
    // PS2_SONY_CODEC_INTERNAL::DecodeFrame (0x003035e0): a próxima imagem.
    void decode_frame() {
        if (dec.take(nv12)) {
            bgra.resize(size_t(nv12.width) * size_t(nv12.height) * 4);
            nf::nv12_to_bgra(nv12.data.data(), nv12.width, nv12.height, bgra.data());
        }
        ++count;
        ++g_stats.frames_decoded;
    }
    // [driving] 0x00303678 PS2_SONY_CODEC_INTERNAL::GetFrame (pelo RCMP::DECODER::GetFrame 0x003029f0): decodifica até
    // o quadro `target` (pelo menos uma imagem); false quando o fluxo acabou.
    bool get_frame(uint32_t target) {
        bool have = false;
        for (;;) {
            have = false;
            if (finished()) return false;
            decode_frame();
            have = true;
            if (count >= target) break;
        }
        return have;
    }
};

// O filme que toca: o PlayMPC, o AV_PLAYER, o AUDIO_PLAYER e o relógio com o leiaute do PS2, e o lado do port.
struct Movie {
    PlayMpcData play;
    AvPlayerData av;
    AudioPlayerData audio;
    AvMsTimer timer;
    MpcFile file;
    Decoder decoder;
    std::string name;
    std::vector<uint8_t> snd_mem;
    int32_t big_id = -1;
    bool big_added = false;
};

void note(uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    mission_note_call(fn, a0, a1, a2, a3);
}

void vblank() {
    ++g_stats.vblanks;
    if (g_host.vblank) g_host.vblank();
}

// O SYNCTASK_run(0) + THREAD_yield(0): as tarefas do jogo (o SNDSYS_service do ASystem) e um VBlank.
void yield() {
    if ((SNDSYS_inited() != 0)) SNDSYS_service();
    vblank();
}

// [driving] 0x003019d0 RCMP::AUDIO_PLAYER::AUDIO_PLAYER(int, int) (o port: um SNDSTRM comum sobre o próprio MPC no
// lugar do SNDSTRM_createtap e do SNDSTRM_queuerequestid; ver o cabeçalho)
void AUDIO_PLAYER_Construct(Movie& m) {
    AudioPlayerData& a = m.audio;
    a.stream = -1;
    a.request = -1;
    if (!(SNDSYS_inited() != 0)) {  // FUN_003077b0 (o SND pronto) falhou: FUN_003070f0 (o erro do SND)
        ++g_stats.no_sound;
        return;
    }
    a.pad08 = 0;
    SndPlayOpts opts{};
    SNDplaysetdef(&opts);
    const int32_t size = SNDSTRM_overhead(1, 0x1e) + 0x64000;  // o buffer do STREAM do AV_PLAYER (o Init: 0x64000)
    m.snd_mem.assign(size_t(size) + 0x400, 0);
    uintptr_t at = (reinterpret_cast<uintptr_t>(m.snd_mem.data()) + 0x3ff) & ~uintptr_t(0x3ff);
    a.mem = reinterpret_cast<uint8_t*>(at);
    a.stream = SNDSTRM_create(&opts, 1, 0x1e, nf::raw(a.mem), size);
    if (a.stream >= 0) a.request = SNDSTRM_queuefile(a.stream, -1, ("|" + m.name).c_str(), 0);
    a.speed = 1.0f;
}

// [driving] 0x00301ac0 RCMP::AUDIO_PLAYER::~AUDIO_PLAYER
void AUDIO_PLAYER_Destruct(Movie& m) {
    AudioPlayerData& a = m.audio;
    if (a.mem) {
        if (a.stream >= 0) SNDSTRM_destroy(a.stream);
        a.mem = nullptr;
        m.snd_mem.clear();
    }
}

// [driving] 0x00301b80 RCMP::AUDIO_PLAYER::StartSound
int32_t AUDIO_PLAYER_StartSound(AudioPlayerData& a) {
    if (a.request == -1) return 0;
    SNDSYS_entercritical();
    const int32_t r = SNDSTRM_modifyhold(a.request, 0);
    SNDSYS_leavecritical();
    return r;
}

// [driving] 0x00301bd0 RCMP::AUDIO_PLAYER::SetVol
int32_t AUDIO_PLAYER_SetVol(AudioPlayerData& a, int32_t vol) {
    if (a.stream == -1) return 0;
    SNDSYS_entercritical();
    const int32_t r = SNDSTRM_vol(a.stream, vol);
    SNDSYS_leavecritical();
    return r;
}

// [driving] 0x00301b20 RCMP::AUDIO_PLAYER::SetSpeed (o SNDSTRM_pitchmult)
int32_t AUDIO_PLAYER_SetSpeed(AudioPlayerData& a, int32_t speed) {
    if (a.stream == -1) return 0;
    SNDSYS_entercritical();
    const int32_t r = SNDSTRM_pitchmult(a.stream, speed);
    SNDSYS_leavecritical();
    return r;
}

// [driving] 0x00301588 RCMP::AV_PLAYER::SetVol
int32_t AV_PLAYER_SetVol(Movie& m, int32_t vol) { return m.av.audio ? AUDIO_PLAYER_SetVol(m.audio, vol) : 0; }

// [driving] 0x00300b88 RCMP::AV_PLAYER::Init (o modo 0: o STREAM_queuefile do arquivo; o port lê o MPC inteiro)
bool AV_PLAYER_Init(Movie& m) {
    std::memset(&m.av, 0, sizeof m.av);
    m.av.mode = 0;
    m.av.no_hold = 0;
    std::vector<uint8_t> bytes;
    if (!g_host.load || !g_host.load(m.name, bytes) || !m.file.parse(std::move(bytes))) return false;
    // Os BIG do FILESYS para o SNDSTRM ler o som do mesmo arquivo (só do port).
    if (FILESYS_existssync("driving/MISC.viv") != 0) m.big_added = FILESYS_addbigsync("driving/MISC.viv", &m.big_id) != 0;
    return m.decoder.open(m.file);
}

// [driving] 0x003010c0 RCMP::AV_PLAYER::GetFirstFrame(unsigned int, int)
bool AV_PLAYER_GetFirstFrame(Movie& m, int32_t offset_ms) {
    AvPlayerData& av = m.av;
    note(0x3061d0);  // STREAM_setpriority x 2 (o port: nada)
    if (av.no_hold == 0) {
        AUDIO_PLAYER_Construct(m);
        av.audio = &m.audio;
        av.has_audio = m.audio.stream < 0 ? 0 : 1;  // ~stream >> 31
    } else {
        av.has_audio = 0;
    }
    av.decoder = 1;
    const bool frame = m.decoder.get_frame(0);
    m.timer = AvMsTimer();
    m.timer.ticks = 0;
    m.timer.last = TIMER_gettick();
    m.timer.speed = 0x1000;
    av.timer = &m.timer;
    AV_MS_TIMER_Update(m.timer);
    av.offset_ms = offset_ms;
    av.last_audio_ms = 0;
    av.drift = 0;
    av.base_ms = int32_t(uint32_t(int32_t(m.timer.ticks * 1000)) / uint32_t(kTimerFrequency));
    if (av.audio) AUDIO_PLAYER_StartSound(m.audio);
    AV_PLAYER_SetSpeed(av, 0x1000);
    return frame;
}

// [driving] 0x00301338 RCMP::AV_PLAYER::GetFrame(float)
bool AV_PLAYER_GetFrame(Movie& m, float target) {
    m.av.target = target;
    const uint32_t n = target <= 0.0f ? 0u : uint32_t(target);  // fptoui
    const bool f = m.decoder.get_frame(n);
    m.av.frame = m.decoder.count;  // RCMP::DECODER::GetCurrentFrameNumber (0x00302970, 0x003035c8)
    return f;
}

// [driving] 0x00300fa0 RCMP::AV_PLAYER::~AV_PLAYER
void AV_PLAYER_Destruct(Movie& m) {
    m.av.decoder = 0;
    if (m.av.audio) {
        AUDIO_PLAYER_Destruct(m);
        m.av.audio = nullptr;
    }
    m.av.timer = nullptr;
    if (m.big_added) FILESYS_delbigsync(m.big_id);
    m.big_added = false;
}

// [driving] 0x00300110 PlayMPC::PlayMPC (o RCMP_SYSTEM::SetREALDefaults 0x00302e00: anotado)
void PlayMPC_Construct(PlayMpcData& p, uint32_t device, uint32_t context, int32_t port) {
    p.buttons = 0;
    p.frame = 0;
    p.stop = 0;
    p.av = nullptr;
    p.device = device;
    p.context = context;
    p.port = port;
    note(0x302e00, 0x3562d0);
}

// [driving] 0x003001d0 PlayMPC::Init(char *, unsigned int, bool)
bool PlayMPC_Init(Movie& m, const char* name, int32_t volume) {
    if (name == nullptr) return false;
    m.name = name;
    // AV_PLAYER::AV_PLAYER(nome, 0x64000, falso, 0) (0x00300a80 -> Init)
    if (!AV_PLAYER_Init(m)) return false;
    m.play.av = &m.av;
    m.play.volume = volume;
    return true;
}

// [driving] 0x00300178 PlayMPC::~PlayMPC
void PlayMPC_Destruct(Movie& m) {
    if (m.play.av) {
        AV_PLAYER_Destruct(m);
        m.play.av = nullptr;
    }
}

// O EndFrame do EAGL com o quadro do filme e as legendas, para o anfitrião.
void present(const Movie* m, const GsPacket* subs) {
    MovieFrameView v;
    if (m && !m->decoder.bgra.empty()) {
        v.width = m->file.width;
        v.height = m->file.height;
        v.bgra = m->decoder.bgra.data();
        v.frame = m->play.frame;
    }
    v.subtitles = subs;
    if (g_host.present) g_host.present(v);
}

// Espera o TIMER andar n tiques (o laço do começo do Play: TIMER_gettick < t + 10).
void wait_ticks(int32_t n) {
    const int32_t t = TIMER_gettick();
    for (int guard = 0; TIMER_gettick() < t + n && guard < 600; ++guard) vblank();
}

// [driving] 0x00300298 PlayMPC::Play(EAGL::ViewPort *, bool, void (*)(unsigned int)) (com o desenho e a espera do lado
// do anfitrião)
void PlayMPC_Play(Movie& m, bool subtitles) {
    PlayMpcData& p = m.play;
    if (!p.av) return;
    wait_ticks(10);
    note(0x2874a8, p.device, 0x6400, 0x3200);
    wait_ticks(10);
    for (int i = 0; i < 2; ++i) present(nullptr, nullptr);  // dois EndFrame com a tela limpa
    bool frame = AV_PLAYER_GetFirstFrame(m, 0x21);
    AV_PLAYER_SetVol(m, p.volume);
    p.frame += 1;
    AV_PLAYER_SetSpeed(m.av, 0);       // RCMP::AV_PLAYER::Pause
    // RCMP::EAGL_VD_create: o quadro vai ao anfitrião (o EAGL_VD_PS2_SINGLE_BUFFERD).
    p.buttons = g_host.pad ? g_host.pad() : 0;
    AV_PLAYER_SetSpeed(m.av, 0x1000);  // RCMP::AV_PLAYER::UnPause
    const float fps = m.file.frame_rate;
    GsPacket subs;
    do {
        subs.words.clear();
        if (subtitles) DrawSubtitlesCallback(uint32_t(p.frame), subs);
        present(&m, subtitles && !subs.words.empty() ? &subs : nullptr);
        ++g_stats.frames_shown;
        while (!AV_PLAYER_IsTimeForDecode(m.av, fps)) yield();
        PlayMPC_ReadInput(p, g_host.pad ? g_host.pad() : 0);
        frame = AV_PLAYER_GetFrame(m, m.av.target);
        p.frame = int32_t(m.av.frame);
    } while (frame && p.stop == 0);
    if (p.stop) ++g_stats.skipped;
    g_stats.last_frames = p.frame;
    AV_PLAYER_Destruct(m);
    p.av = nullptr;
    for (int i = 0; i < 2; ++i) present(nullptr, nullptr);
}

// O anfitrião: se não há quem desenhe, o filme não toca (as capturas sem filmes: o 0x0032e688 a 0).
bool movies_on() { return g_host.enabled && g_host.present != nullptr; }

}  // namespace

MovieHost& movie_host() { return g_host; }
std::function<int32_t(int32_t)>& av_audio_probe() {
    static std::function<int32_t(int32_t)> f;
    return f;
}
MovieStats& movie_stats() { return g_stats; }

void movie_power_on() {
    g_stats = MovieStats();
}

bool MpcFile::parse(std::vector<uint8_t> bytes) {
    data = std::move(bytes);
    video.clear();
    audio_chunks = 0;
    width = height = 0;
    frame_rate = 0.0f;
    size_t p = 0;
    while (p + 8 <= data.size()) {
        const uint32_t tag = rd32(&data[p]), size = rd32(&data[p + 4]);
        if (size < 8 || p + size > data.size()) break;
        if (tag == 0x6843504du) {  // 'MPCh'
            video.push_back({uint32_t(p + 8), size - 8});
            const uint8_t* v = &data[p + 8];
            if (width == 0 && size >= 16 && v[0] == 0 && v[1] == 0 && v[2] == 1 && v[3] == 0xb3) {
                width = (v[4] << 4) | (v[5] >> 4);
                height = ((v[5] & 0xf) << 8) | v[6];
                const uint32_t code = v[7] & 0xf;
                if (code < 9) std::memcpy(&frame_rate, &kFrameRates[code], 4);
            }
        } else if ((tag & 0xffffu) == 0x4353u) {  // 'SC..'
            ++audio_chunks;
        }
        p += size;
    }
    return !video.empty() && width > 0 && height > 0 && frame_rate > 0.0f;
}

void AV_MS_TIMER_Update(AvMsTimer& t) {
    const int32_t now = TIMER_gettick();
    uint64_t d;
    if (t.speed == 0x1000) {
        d = uint64_t(uint32_t(now - t.last));
    } else {
        d = (uint64_t(uint32_t(t.speed)) * uint64_t(uint32_t(now - t.last))) >> 12;
    }
    t.ticks = int64_t(uint64_t(t.ticks) + d);
    t.last = now;
}

int32_t AV_PLAYER_SyncedAudioTime(AvPlayerData& av) {
    AvMsTimer& t = *nf::raw(av.timer);
    AV_MS_TIMER_Update(t);
    // ((ticks x 125) << 3) nos 32 bits de baixo, sem sinal, pela frequência do TIMER.
    const uint32_t ms = uint32_t(uint64_t(t.ticks) * 1000u) / uint32_t(kTimerFrequency);
    int32_t r = int32_t(ms) - av.base_ms;
    if (av.has_audio != 0 && !av.audio) ++g_stats.guards;  // o original leria o +0xc do AUDIO_PLAYER nulo
    if (av.has_audio != 0 && av.audio) {
        const AudioPlayerData& a = *nf::raw(av.audio);
        int32_t played = 0;
        if (av_audio_probe()) {
            played = av_audio_probe()(a.stream);
        } else {
            SNDSYS_entercritical();
            SndStreamStatus st{};
            SndRequestStatus rs{};
            SNDSTRM_status(a.stream, &st);
            SNDSTRM_requeststatus(st.current, &rs);
            SNDSYS_leavecritical();
            played = rs.played_ms;
        }
        const int32_t audio_ms = played - 0;  // menos a latência do SND (0x00307f70 -> 0x00310f60: 0)
        if (av.last_audio_ms < audio_ms) {
            av.last_audio_ms = audio_ms;
            const int32_t d = av.drift;
            const int32_t nd = d - (d < 0 ? (d + 7) >> 3 : d >> 3) + (audio_ms - r);
            av.drift = nd;
            const int32_t mag = nd < 0 ? -nd : nd;
            if (mag >= 0x109) {
                av.drift = 0;
                av.base_ms -= nd < 0 ? (nd + 7) >> 3 : nd >> 3;
                r = int32_t(ms) - av.base_ms;
            }
        }
    }
    return r;
}

bool AV_PLAYER_IsTimeForDecode(AvPlayerData& av, float fps) {
    const int32_t t = AV_PLAYER_SyncedAudioTime(av);
    // (float)(uint)(t + offset) x 0,001 x fps
    const uint32_t u = uint32_t(t + av.offset_ms);
    float fu;
    if (int32_t(u) >= 0) {
        fu = ee::cvt_s(int32_t(u));
    } else {
        const float h = ee::cvt_s(int32_t((u >> 1) | (u & 1)));
        fu = ee::add(h, h);
    }
    av.target = ee::mul(ee::mul(fu, 0.001f), fps);
    float ff;
    if (int32_t(av.frame) >= 0) {
        ff = ee::cvt_s(int32_t(av.frame));
    } else {
        const float h = ee::cvt_s(int32_t((av.frame >> 1) | (av.frame & 1)));
        ff = ee::add(h, h);
    }
    return ee::le(ee::add(ff, 1.0f), av.target);
}

int32_t AV_PLAYER_SetSpeed(AvPlayerData& av, uint32_t speed) {
    const int32_t s = speed < 0x4001 ? int32_t(speed) : 0x4000;
    if (av.audio) AUDIO_PLAYER_SetSpeed(*nf::raw(av.audio), s);
    AvMsTimer& t = *nf::raw(av.timer);
    const int32_t now = TIMER_gettick();
    uint64_t d;
    if (t.speed == 0x1000) {
        d = uint64_t(uint32_t(now - t.last));
    } else {
        d = (uint64_t(uint32_t(t.speed)) * uint64_t(uint32_t(now - t.last))) >> 12;
    }
    t.ticks = int64_t(uint64_t(t.ticks) + d);
    t.speed = s;
    t.last = now;
    return 0;
}

void PlayMPC_ReadInput(PlayMpcData& p, uint16_t buttons) {
    // PAD_update + PAD_getdataptr(porta) +0xc
    if (p.buttons != buttons) {
        if ((buttons & 0x840) != 0) p.stop = 1;
        p.buttons = buttons;
    }
}

const char* GLocale_GetSubDirectory() {
    // GetLanguage 0 (o inglês) e GetFoundationVideoMode 1 (NTSC): a tabela 0x0033ba20.
    return "ntsc\\eng";
}

void PlayMovie(const char* path, const char* sub) {
    if (!movies_on()) return;
    Movie m;
    PlayMPC_Construct(m.play, 0, 0, uber_bond().word(0x990));
    const bool ok = PlayMPC_Init(m, path, (uber_bond().word(0x964) * 0x7f) / 100);
    note(0x291750);              // EAGL::RenderContext::NewViewPort
    note(0x2955d0);              // a janela da vista (a tela inteira)
    note(0x295db8);              // SNDPLATFORM_restore
    note(0x2495e8, 0x376880);    // GLoadingScreen::Status("Play Movie")
    note(0x167e10, 0, 0);        // IOModule::EnableUpdating(0)
    IFeedback_Pause();
    ASoundManager_Pause();
    note(0x165900);              // ActionQueueManager::FlushAllQueues
    note(0x1ac118, 0, 0);        // RRenderer::Flush
    if (!ok) {
        ++g_stats.missing;
    } else {
        ++g_stats.played;
        g_stats.last = path;
        g_stats.last_step = simulation().steps;
    }
    GSubtitlesData* subs = GSubtitles_TheApp();
    if (sub != nullptr && subs != nullptr) {
        GSubtitles_LoadSubtitles(*subs, sub);
        PlayMPC_Play(m, true);  // o DrawSubtitlesCallback (0x0024a088)
        GSubtitles_ClearSubtitles(*subs);
    } else {
        PlayMPC_Play(m, false);
    }
    note(0x2917a8);              // EAGL::RenderContext::DeleteViewPort
    PlayMPC_Destruct(m);
    note(0x167e10, 0, 1);        // IOModule +8 = 1, EnableUpdating(1)
    note(0x165900);
    ASoundManager_Resume();
}

void ESetVideo_Execute(const char* name, const char* sub) {
    note(0x2495e8, 0x36fab0);  // GLoadingScreen::Status
    if (movies_on() && name != nullptr) {
        std::string path = std::string(GLocale_GetSubDirectory()) + "\\" + name;
        // sprintf(buf + strlen - 3, "mpc"): as três últimas letras viram "mpc"
        if (path.size() >= 3) path.resize(path.size() - 3);
        path += "mpc";
        Movie m;
        PlayMPC_Construct(m.play, 0, 0, uber_bond().word(0x990));
        const bool ok = PlayMPC_Init(m, path.c_str(), (uber_bond().word(0x964) * 0x7f) / 100);
        note(0x291750);
        note(0x2955d0);
        note(0x295db8);
        note(0x167e10, 0, 0);
        IFeedback_Pause();
        note(0x165900);
        ASoundManager_Pause();
        if (!ok) {
            ++g_stats.missing;
        } else {
            ++g_stats.played;
            g_stats.last = path;
            g_stats.last_step = simulation().steps;
        }
        GSubtitlesData* subs = GSubtitles_TheApp();
        if (sub == nullptr || !g_host.subtitles || subs == nullptr) {
            PlayMPC_Play(m, false);
        } else {
            GSubtitles_LoadSubtitles(*subs, ("data\\loading\\" + std::string(sub)).c_str());
            PlayMPC_Play(m, true);  // o DrawSubtitlesCallback (0x0024a088)
            GSubtitles_ClearSubtitles(*subs);
        }
        note(0x2917a8);
        PlayMPC_Destruct(m);
        note(0x167e10, 0, 1);
        note(0x165900);
        ASoundManager_Resume();
    }
    note(0x2495e8, 0x36fae8);
}

MovieChoice RunTheGame_IntroMovie(const char* track) {
    MovieChoice c;
    if (track == nullptr) return c;
    struct Row {
        const char* track;
        const char* mad;
        const char* sub;
    };
    // As comparações do RunTheGame (0x00175ef0...): a pista, o filme (o ".mad" vira "mpc") e as legendas.
    static const Row kRows[] = {
        {"paris_mis01", "paris_intro.mad", "data\\loading\\paris_intro.sub"},
        {"uw_mis11", "island_intro2.mad", "data\\loading\\island_intro2.sub"},
        {"snow1a_mis3", "snow_intro.mad", "data\\loading\\snow_intro.sub"},
        {"snow2a_mis4", "alps_intro.mad", "data\\loading\\alps_intro.sub"},
        {"junglea_mis13a", "ja_intro.mad", nullptr},
        {"junglec_mis13c", "jc_intro.mad", "data\\loading\\jc_intro.sub"},
    };
    for (const Row& r : kRows)
        if (std::strcmp(track, r.track) == 0) {
            c.path = std::string(GLocale_GetSubDirectory()) + "\\" + r.mad;
            c.path.resize(c.path.size() - 3);
            c.path += "mpc";
            if (r.sub) c.sub = r.sub;
            return c;
        }
    return c;
}

MovieChoice RunTheGame_OutroMovie(const char* track) {
    MovieChoice c;
    if (track == nullptr) return c;
    struct Row {
        const char* track;
        const char* mad;
        const char* sub;
    };
    static const Row kRows[] = {
        {"paris_mis01", "paris_outro.mad", "data\\loading\\paris_outro.sub"},
        {"uw_mis11", "island_outro.mad", "data\\loading\\island_outro.sub"},
        {"snow1a_mis3", "snow_outro.mad", "data\\loading\\snow_outro.sub"},
        {"junglea_mis13a", "ja_outro.mad", nullptr},
        {"junglec_mis13c", "jc_outro.mad", nullptr},
    };
    for (const Row& r : kRows)
        if (std::strcmp(track, r.track) == 0) {
            c.path = std::string(GLocale_GetSubDirectory()) + "\\" + r.mad;
            c.path.resize(c.path.size() - 3);
            c.path += "mpc";
            if (r.sub) c.sub = r.sub;
            return c;
        }
    return c;
}

bool RunTheGame_PlaysOutro() {
    UberBondBlock& u = uber_bond();
    return u.word(0x978) == 0 || u.word(0x5d8) != 0;
}

void RunTheGame_PlayIntro(const char* track) {
    // Depois do GLoadingScreen::Shutdown e do GSubtitles na pilha (o port: o GSubtitles do anfitrião): com os filmes
    // (0x0032e688) e um nome; as legendas só com o 0x0032e694.
    const MovieChoice c = RunTheGame_IntroMovie(track);
    if (!movies_on() || c.path.empty()) return;
    PlayMovie(c.path.c_str(), g_host.subtitles && !c.sub.empty() ? c.sub.c_str() : nullptr);
}

void RunTheGame_PlayOutro(const char* track) {
    const MovieChoice c = RunTheGame_OutroMovie(track);
    if (!movies_on() || c.path.empty() || !RunTheGame_PlaysOutro()) return;
    PlayMovie(c.path.c_str(), g_host.subtitles && !c.sub.empty() ? c.sub.c_str() : nullptr);
}

bool movie_event_execute(const PendingEvent& e) {
    if (e.type != 78) return false;
    // [driving] 0x001592b8 ESetVideo::ESetVideo(char *, char *): o nome e as legendas (as palavras 1 e 2, resolvidas).
    ESetVideo_Execute(nf::p32_ptr<const char>(e.obj[1]), nf::p32_ptr<const char>(e.obj[2]));
    return true;
}

}  // namespace drv
