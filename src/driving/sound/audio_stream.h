#pragma once
// Os fluxos do jogo de carro (BondAudioPS2.lib do DRIVING.ELF: AStream, AStreamPriv, AFader) e os eventos de som da
// missão, portados do MIPS e conferidos na parte 23 do nf_driving_check. docs/driving.md seção 27.
//
// O ASoundManager::Init cria dois fluxos: "Music" (o BIG "driving/MIS01.mus": a música, um .asf por trecho) e "Speech"
// (o "driving/MIS01EN.spe": as falas e os ambientes da cidade), e o AFader "SpeechVsAmbience" que divide o fluxo das
// falas entre a fala (a primária: EStreamEvent "speech", em fila) e o ambiente (a secundária: o nome do EnviroDesc sob
// o carro, "city", "crowds", "residential", tocado em laço quando nenhuma fala toca, com as trocas suavizadas). Cada
// AStream é um ABaseSound (o BuildPaths o toca a cada quadro, a uma distância "infinita": o max_distance 0): o
// AStream::Play manda ao SNDSTRM o volume, o pitch e o passa-baixa, e a fila de eventos (AStream::Event: o nome, o tempo
// de transição, laço, efeito, "segurado") vira pedidos ao SNDSTRM um quadro por vez (um evento em laço é pedido de novo
// quando só ele resta na fila do SND). Um evento "segurado" (prebuffer) espera o EReleaseStream (ou 10 s).
//
// Eventos: 20 EStreamEvent (music, speech, nis), 273 EReleaseStream, 241 ESetAudioMix, 266 EResetAudioMix, 164
// EPlaySoundPos (um AOneShotSound parado num ponto do mundo).
//
// Divergências: a fila de eventos do AStream é um std::deque (no PS2 a deque do GCC 2.95 no heap do jogo: o porte não
// guarda o AStream no leiaute do PS2, como o resto da camada de som, driving/sound/audio_internal.h); um AStream::Get
// de um nome que não existe devolve nulo e o evento não faz nada (o original chamaria o Event num objeto nulo), contado
// em audio_stream_stats().guards; quando o SNDSTRM_queuefile recusa o pedido (-13, os quatro pedidos ocupados) o
// DecodeError chama o Stop, que esvazia a fila, e o AStream::Play original tira a frente da deque vazia (o começo passa
// do fim e as entradas mortas voltam a ser lidas): o porte deixa a fila vazia e conta em guards (a parte 23 repara a
// deque do original do mesmo jeito e segue conferindo).

#include <cstdint>
#include <string>
#include <vector>

namespace drv {

struct PendingEvent;
struct WWorldPos;
struct CarpInstanceRec;

// O ambiente do mundo ('Envi' do grupo 'Map ' do CARP da pista: 0x1c bytes cada; +4 o modo do AFX, +8 o nome do
// ambiente em fluxo, +0x14 o topo da faixa de altura, +0x18 a altura).
struct WWorldEnviro {
    const CarpInstanceRec* instances = nullptr;  // WWorld +0x20 (as 'in')
    int32_t count = 0;                           // WWorld +0x2c
    const uint8_t* table = nullptr;              // WWorld +0x30
};
// [driving] 0x00230bb8 WWorld::GetEnviroDesc(WWorldPos &) const
const uint8_t* WWorld_GetEnviroDesc(const WWorldEnviro& w, const WWorldPos& wp);

// Os eventos de som (o EventManager::RunEvents; driving/game/events.cpp os chama): true se o número é de som.
bool audio_event_execute(const PendingEvent& e);
// [driving] 0x002ec968 ASoundManager::SetMissionOver: a música some em 3 s e não volta.
void audio_set_mission_over();

// Para a conferência (a parte 23 do nf_driving_check): o estado de um AStream e do AFader e as operações do porte por
// nome, como o jogo as chama.
struct AStreamEntryView {
    std::string name;
    float fade = 0;
    int32_t loop = 0, effect = 0, held = 0, tick = 0;
};
struct AStreamView {
    bool exists = false;
    float volume = 0, pitch = 0, max_distance = 0, max_distance2 = 0;  // o ABaseSound (+0x6c, +0x70, +0x60, +0x64)
    std::string mix;                                                    // o nome da mixagem (+0x84)
    int32_t hold_timeout = 0, held_ticks = 0, release_tick = 0;         // +0xb4..+0xbc
    std::vector<AStreamEntryView> queue;                                // AStreamPriv +0x00
    int32_t status[3] = {}, rstatus[4] = {};                            // +0x28, +0x34
    std::string file;                                                   // +0x44 (vazio: nulo)
    float filter = 0, fade = 0, priv_volume = 0;                        // +0x48, +0x4c, +0x50
    int32_t stream = -1, req = -1, event_tick = 0, play_tick = 0;      // +0x58..+0x64
    int32_t over = 0, not_found = 0, paused = 0, loop = 0, effect = 0, held = 0;  // +0x70..+0x84
    uint8_t* mem = nullptr;                                             // +0x88 (a memória do SNDSTRM)
    std::string name;                                                   // +0x10c
};
AStreamView astream_view(const char* name);
void astream_test_play(const char* name, float vol, float pitch, float azimuth);
void astream_test_event(const char* name, float fade, const char* file, bool loop, bool effect, bool held);
void astream_test_next(const char* name);
void astream_test_last(const char* name);
void astream_test_stop(const char* name);
void astream_test_fadeout(const char* name);
float astream_test_latency(const char* name);
struct AFaderView {
    bool exists = false;
    float level = 0;
    std::string second;
    float filter = 0;
    int32_t primary = 0, restart = 0, fading = 0, call911 = 0, allow_second = 0, second_on = 0, w40 = 0;
};
AFaderView afader_view(const char* name);
void afader_test_update(const char* name);
int32_t afader_test_event(const char* name, float fade, const char* file, bool effect, bool held);
void afader_test_set_secondary(const char* name, const char* second);
void afader_test_call911(const char* name);
// Os globais da camada: AStream::fTrigger e fHoldBegin, ASoundManager::fMissionOver, fHUDFade e fgIsPaused.
struct AStreamGlobalsView {
    int32_t trigger = 0, hold_begin = 0, mission_over = 0, hud_fade = 0, is_paused = 0;
};
AStreamGlobalsView astream_globals();
void astream_set_globals(const AStreamGlobalsView& g);

// Para os relatórios e a conferência.
struct AudioStreamStats {
    int32_t music_req = -1, speech_req = -1;   // o pedido do SNDSTRM em curso
    std::string music_file, speech_file;      // o arquivo em curso ("|all.asf")
    int32_t music_queue = 0, speech_queue = 0;  // os eventos na fila
    float music_volume = 0, speech_volume = 0;  // o volume do último Play
    float fader_level = 0;
    std::string ambience;                      // a secundária do AFader
    uint64_t events = 0, queued = 0, failed = 0, guards = 0;
    uint64_t mix_voices = 0;                   // as vozes do mixador do EE tocando
};
AudioStreamStats audio_stream_stats();

}  // namespace drv
