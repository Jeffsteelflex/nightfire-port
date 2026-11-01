#pragma once
// Os filmes das missões de carro (docs/driving.md seção 28): o PlayMPC e o RCMP da EA no DRIVING.ELF (o AV_PLAYER, o
// AUDIO_PLAYER, o relógio AV_MS_TIMER, o DECODER), o evento ESetVideo (78) e o filme de abertura e o de fim que o
// RunTheGame toca antes e depois do laço do jogo.
//
// Os arquivos: ntsc\eng\<nome>.mpc no MISC.VIV (Paris: paris_intro, paris2, paris_outro). Um MPC é uma sequência de
// pedaços {etiqueta de 4 bytes, u32 LE o tamanho com o cabeçalho}: 'MPCh' traz o vídeo elementar MPEG-2 (uma imagem
// por pedaço; o primeiro com o cabeçalho de sequência: 640 x 448, 29,97 quadros por segundo, imagens B) e os 'SCHl',
// 'SCCl', 'SCDl' e 'SCEl' o som da EA, como num .asf (EA-XA, o cabeçalho PT). As legendas: data\loading\<nome>.sub
// (driving/render/subtitles.h).
//
// Como o PS2 toca: o ESetVideo::~ESetVideo (e a função sem nome 0x00175af8 do RunTheGame) monta o PlayMPC, o Init cria
// o AV_PLAYER (um STREAM do librealfilez com dois canais: 'M' para o vídeo e 'SC' para o som), pausa a entrada, a
// vibração e o som do jogo e chama o Play, que só volta no fim do filme: o primeiro quadro (o GetFirstFrame: o
// AUDIO_PLAYER, um SNDSTRM "tap" com o pedido segurado, o relógio em milissegundos do TIMER de 100 Hz, o som solto),
// e a cada volta o quadro desenhado (o EAGL_VD), as legendas (o DrawSubtitlesCallback com o número do quadro), a
// espera até o relógio sincronizado com o som pedir o próximo quadro (IsTimeForDecode: (o tempo do som + 33 ms) x
// quadros por segundo passa do quadro + 1), a leitura do controle (Start ou X param o filme) e o GetFrame, que
// decodifica até alcançar o quadro do relógio (pula quadros atrasados). O filme acaba quando o decodificador não dá
// mais quadros.
//
// No port: o vídeo pelo decodificador de MPEG-2 do port (platform/mpeg2_decoder.h, pela fachada nf::Mpeg2Decoder com a
// escolha "own": o mesmo resultado em todas as plataformas) e a conversão do IPU (nf::nv12_to_bgra), o lugar da libmpeg
// da Sony; o som pelo SND do port (driving/sound/snd_stream.h: o SCHl e os SCDl do MPC no SNDSTRM, ao mixador do EE e à
// SPU2). O laço do Play fica do lado de quem desenha (MovieHost): a cada volta o anfitrião desenha o quadro e as
// legendas, e a espera é o tempo do som andando um VBlank (1/60 s) de cada vez.
//
// Divergências (só do port; também nas funções):
//  - o STREAM de dois canais e o SNDSTRM "tap" (SNDSTRM_createtap 0x00309d18, SNDSTRM_queuerequestid 0x00309cf0) dão
//    lugar a um SNDSTRM comum que lê o próprio MPC (o SNDSTRMI_parsechunk solta os pedaços 'MPCh' e 'SCCl'): as mesmas
//    amostras, o mesmo segurar e soltar;
//  - o decodificador da Sony (PS2_SONY_CODEC_INTERNAL: sceMpegGetPicture, a CSC do IPU) é o do port; o número do quadro
//    conta as imagens que saíram, como o +0x10 do codec;
//  - o PlayMPC não aloca o AV_PLAYER no heap do REAL (o RCMP_SYSTEM::SetREALDefaults): o port guarda os objetos ao lado
//    (os campos com o leiaute do PS2 para a conferência);
//  - o EAGL_VD (a imagem num SPRITE do GS sobre a tela) é o anfitrião: o quadro inteiro na área 4:3 da janela.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "driving/render/ea_font.h"
#include "platform/ptr32.h"

namespace drv {

// ---- o arquivo (só do port)
struct MpcFile {
    struct Unit {
        uint32_t offset = 0, size = 0;
    };
    std::vector<uint8_t> data;
    std::vector<Unit> video;      // o vídeo de cada 'MPCh' (sem o cabeçalho de 8 bytes)
    uint32_t audio_chunks = 0;    // 'SCHl', 'SCCl', 'SCDl', 'SCEl'
    int width = 0, height = 0;
    float frame_rate = 0.0f;      // a do cabeçalho de sequência (o frame_rate_code)
    bool parse(std::vector<uint8_t> bytes);
};

// ---- os objetos com o leiaute do PS2
// RCMP::AV_MS_TIMER (0x18): o relógio em tiques do TIMER, com a velocidade em 4.12.
struct AvMsTimer {
    int32_t last = 0;             // +0x00 o TIMER_gettick da última conta
    int32_t pad04 = 0;
    int64_t ticks = 0;            // +0x08 os tiques acumulados (vezes a velocidade)
    int32_t speed = 0x1000;       // +0x10
    int32_t pad14 = 0;
};
static_assert(sizeof(AvMsTimer) == 0x18);

// RCMP::AUDIO_PLAYER (0x14).
struct AudioPlayerData {
    float speed = 1.0f;           // +0x00
    P32<uint8_t> mem;             // +0x04 a memória do SNDSTRM
    int32_t pad08 = 0;
    int32_t stream = -1;          // +0x0c o fluxo do SND
    int32_t request = -1;         // +0x10 o pedido (segurado até o StartSound)
};
static_assert(sizeof(AudioPlayerData) == 0x14);

// RCMP::AV_PLAYER (0x68).
struct AvPlayerData {
    P32<AudioPlayerData> audio;   // +0x00
    uint32_t buf1 = 0;            // +0x04 a memória do STREAM
    uint32_t buf2 = 0;            // +0x08
    uint32_t name = 0;            // +0x0c
    uint32_t name2 = 0;           // +0x10 o som num arquivo à parte (Paris: 0)
    int32_t separate = 0;         // +0x14
    int32_t mode = 0;             // +0x18 0 o STREAM_queuefile, 1 a carga inteira, 2 a memória
    int32_t no_hold = 0;          // +0x1c
    uint32_t async[4] = {};       // +0x20..+0x2c
    uint32_t stream = 0;          // +0x30 o STREAM do vídeo e do som
    uint32_t stream2 = 0;         // +0x34
    uint32_t request = 0;         // +0x38
    uint32_t request2 = 0;        // +0x3c
    int32_t offset_ms = 0;        // +0x40 o adiantamento do vídeo (33 ms)
    uint32_t frame = 0;           // +0x44 o quadro do decodificador
    float target = 0.0f;          // +0x48 o quadro que o relógio pede
    int32_t base_ms = 0;          // +0x4c o começo no relógio (corrigido pelo som)
    int32_t last_audio_ms = 0;    // +0x50
    int32_t has_audio = 0;        // +0x54
    int32_t drift = 0;            // +0x58 a diferença acumulada som x relógio
    P32<AvMsTimer> timer;         // +0x5c
    uint32_t self = 0;            // +0x60
    uint32_t decoder = 0;         // +0x64 (o port: o decodificador fica ao lado, MoviePlayer)
};
static_assert(sizeof(AvPlayerData) == 0x68);

// PlayMPC (0x20).
struct PlayMpcData {
    uint32_t device = 0;          // +0x00 o EAGL::Device
    uint32_t context = 0;         // +0x04 o EAGL::RenderContext
    uint16_t buttons = 0;         // +0x08 os botões do controle da última leitura
    uint16_t pad0a = 0;
    int32_t frame = 0;            // +0x0c o quadro mostrado (o callback das legendas o recebe)
    int32_t stop = 0;             // +0x10 o jogador pediu para parar
    P32<AvPlayerData> av;         // +0x14
    int32_t port = 0;             // +0x18 a porta do controle (UberBond +0x990)
    int32_t volume = 0;           // +0x1c 0..127
};
static_assert(sizeof(PlayMpcData) == 0x20);

// ---- o anfitrião (o nf_driving)
struct MovieFrameView {
    int width = 0, height = 0;
    const uint8_t* bgra = nullptr;   // o quadro decodificado (BGRA, o alfa 0x80 da CSC do IPU)
    int frame = 0;                   // o PlayMPC +0xc
    const GsPacket* subtitles = nullptr;  // as legendas desenhadas neste quadro (nulo nenhuma)
};
struct MovieHost {
    // Um arquivo do MISC.VIV ("ntsc\eng\paris2.mpc", "data\loading\paris2.sub"); false se não existe.
    std::function<bool(const std::string& name, std::vector<uint8_t>& out)> load;
    // Um VBlank que passa enquanto o filme espera o relógio (o THREAD_yield): o TIMER, o SND e a saída do som andam.
    std::function<void()> vblank;
    // Os botões do controle (o PAD_getdataptr +0xc do EA: 0x800 Start, 0x40 X) lidos pelo ReadInput.
    std::function<uint16_t()> pad;
    // O quadro desenhado (o EndFrame do Play).
    std::function<void(const MovieFrameView&)> present;
    // Os filmes desligados (o 0x0032e688 a 0): nada toca.
    bool enabled = true;
    // As legendas (o 0x0032e694: o UberBond +0xa2c).
    bool subtitles = false;
};
MovieHost& movie_host();

// O que tocou (o relatório e a conferência).
struct MovieStats {
    uint64_t played = 0, skipped = 0, missing = 0, frames_shown = 0, frames_decoded = 0, vblanks = 0;
    uint64_t subtitle_lines = 0;
    uint64_t guards = 0;
    uint64_t no_sound = 0;       // filmes sem o SND (as capturas sem --audio-dump): o relógio só pelo TIMER
    std::string last;             // o último arquivo
    int last_frames = 0;
    int32_t last_step = -1;      // o passo da simulação do último filme
};
MovieStats& movie_stats();

// ---- o relógio (exposto para a conferência)
// [driving] 0x00301950 RCMP::AV_MS_TIMER::Update
void AV_MS_TIMER_Update(AvMsTimer& t);
// [driving] 0x003015b0 RCMP::AV_PLAYER::SyncedAudioTime (o tempo do som: o played_ms do pedido do fluxo)
int32_t AV_PLAYER_SyncedAudioTime(AvPlayerData& av);
// [driving] 0x00301390 RCMP::AV_PLAYER::IsTimeForDecode
bool AV_PLAYER_IsTimeForDecode(AvPlayerData& av, float fps);
// [driving] 0x00301498 RCMP::AV_PLAYER::SetSpeed (0x00301548 Pause e 0x00301568 UnPause: 0 e 0x1000)
int32_t AV_PLAYER_SetSpeed(AvPlayerData& av, uint32_t speed);
// [driving] 0x003007b0 PlayMPC::ReadInput (os botões do PAD_update/PAD_getdataptr)
void PlayMPC_ReadInput(PlayMpcData& p, uint16_t buttons);
// Só para a conferência (a parte 24): o tempo tocado do pedido corrente do fluxo (o SNDSTRM_status + o
// SNDSTRM_requeststatus) por outra fonte; vazio: o SND do port.
std::function<int32_t(int32_t stream)>& av_audio_probe();
// O TIMER_getfrequency do relógio (o 0x003c80d0: 100).
constexpr int32_t kTimerFrequency = 100;

// ---- tocar
// [driving] 0x00175af8 (sem nome; o nightfire-cxbx: "PlayMovie", chamada duas vezes pelo RunTheGame): o filme `path`
// (já com o diretório e o "mpc") com as legendas `sub` (nulo nenhuma). Volta quando o filme acaba.
void PlayMovie(const char* path, const char* sub);
// [driving] 0x001592d8 ESetVideo::~ESetVideo: o filme `name` ("paris2.mad": as três últimas letras viram "mpc") com as
// legendas `sub` ("paris2.sub", em data\loading).
void ESetVideo_Execute(const char* name, const char* sub);
// O filme de abertura (antes do laço) e o de fim (depois) do RunTheGame 0x00175d78 para a pista (o UberBond +0x4f4):
// o caminho e as legendas, ou vazios sem filme.
struct MovieChoice {
    std::string path, sub;
};
MovieChoice RunTheGame_IntroMovie(const char* track);
MovieChoice RunTheGame_OutroMovie(const char* track);
// O fim do RunTheGame: o filme de fim toca se a missão foi ganha (UberBond +0x978 == 0) ou com o +0x5d8.
bool RunTheGame_PlaysOutro();
// Os dois filmes do RunTheGame com as condições dele (os filmes ligados, o nome, as legendas ligadas).
void RunTheGame_PlayIntro(const char* track);
void RunTheGame_PlayOutro(const char* track);
// [driving] 0x0024a008 GLocale::GetSubDirectory (o modo de vídeo NTSC e a língua 0: "ntsc\eng")
const char* GLocale_GetSubDirectory();

// O evento 78 no executor dos eventos (driving/game/events.cpp): true se era ele.
struct PendingEvent;
bool movie_event_execute(const PendingEvent& e);

void movie_power_on();

}  // namespace drv
