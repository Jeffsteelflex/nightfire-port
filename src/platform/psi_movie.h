#pragma once
// Os filmes (MOVIES/30_FPS/*.PSS; formato em docs/formats.md, "Vídeos"; docs/port.md, "Filmes").
//
// As funções do jogo (ACTION.ELF) estão portadas com os nomes e a ordem do PS2: psiStartBackgroundMovie abre o filme
// (só os de UsingMovie), psiDecodeBackgroundMovieFrame anda um quadro por tick (o main chama antes do GameFlow_Main;
// no fim do vídeo o filme para ou, em laço, recomeça), psiSwapBGFMVFrame lê o arquivo enquanto o main espera o VBlank,
// psiStopBackgroundMovie para, psiMovieFinished é o que o jogo pergunta. Por baixo, no lugar da libmpeg, do IPU e do
// IOP:
//   - o arquivo: PssFile (platform/pss.h), que separa o vídeo e o áudio;
//   - o vídeo: o decodificador de MPEG-2 do Windows (Mpeg2Decoder, platform/movie_decoder.h), na CPU;
//   - o som: o PCM entra na entrada de dados do núcleo 0 da SPU2 (Spu2::input_push), vezes o BVOL que
//     changeInputVolume põe (MovieVolume x 100), e sai pela mistura de sempre (audio_out: o aparelho, um WAV ou nada);
//   - a imagem: movie_image dá o quadro BGRA (512 x 448, alfa 0x80) que o renderizador desenha como fundo do quadro
//     (PS2UploadBGMImage: no PS2 a imagem é copiada para o buffer de quadro antes do resto do desenho).
//
// A sincronia (só do PC): o quadro mostrado é o do som que já saiu da caixa: o que a SPU2 consumiu da entrada menos o
// que ainda está na fila do aparelho (audio_out_queued_frames), em amostras de 48 kHz, 1600 por quadro a 30 fps
// (MovieClock em pss.h). Sem som (os fundos do menu) ou depois que ele acaba, o relógio da própria SPU2 (o que ela
// misturou e já se ouviu, 800 amostras por VBlank do jogo). No PS2 o vídeo anda um
// quadro por tick de 2 VBlanks e o som pelo relógio da SPU2, sem ligação entre os dois. O fim do filme é o fim do
// vídeo (como o sceMpegIsEnd): o último quadro mostrado pelo tempo dele.
//
// Divergências (comentadas nas funções): a memória do filme não sai do heap do jogo (AllocateMovieMemory: ~2.3 MB no
// PS2); o som começa junto com o primeiro quadro (no PS2, depois de 0x6000 bytes no IOP); a armadilha de
// strFileOpen para 07360048 (um laço infinito) não existe; sem filmes ligados (movie_set_enabled) ou sem decodificador,
// psiStartBackgroundMovie age como um arquivo que não abriu (o filme não toca, como antes deste porte).

#include <cstdint>
#include <string>

extern uint32_t BGFMVPlaying;   // 0x0030c69c um filme tocando
extern uint8_t LoopingMovie;    // 0x0030c6a1 o filme recomeça no fim (os fundos das páginas do menu)
extern uint32_t MoviePlaying;   // 0x0030c6a4 o código do filme
extern uint32_t MovieVolume;    // 0x0030c6a8 o volume pedido (SFXGetVolume: 0-100)
extern uint32_t ImageToUpload;  // 0x0030c6b4 há um quadro decodificado para mostrar
extern uint32_t MovieFrame;     // 0x0030c7a0

uint32_t UsingMovie(uint32_t movie);
void psiStartBackgroundMovie(uint32_t movie, int8_t loop, uint32_t volume);
void psiStopBackgroundMovie();
bool psiMovieFinished();
void psiDecodeBackgroundMovieFrame();
int32_t psiSwapBGFMVFrame();
void changeMasterVolume(uint32_t volume);
void changeInputVolume(uint32_t volume);
// O quadro vai para a tela neste quadro? (BGFMVPlaying e ImageToUpload)
bool PS2UploadBGMImage();

namespace nf {

// Ligados (o padrão do jogo: [video] movies = on) ou não (as capturas, os testes, --no-movies: o filme não abre e o
// jogo segue como sem ele).
void movie_set_enabled(bool on);
bool movie_enabled();
// A pasta dos filmes (<dados>\MOVIES\30_FPS).
void movie_set_dir(const std::wstring& dir);
const std::wstring& movie_dir();
// Testes: sem o decodificador (cada quadro do vídeo conta, nenhuma imagem sai).
void movie_set_decode(bool on);

// O quadro a mostrar agora (decodifica o que faltar até ele pelo relógio do som). false: nenhum filme com imagem.
struct MovieImage {
    int width = 0, height = 0;
    const uint8_t* bgra = nullptr;  // width x height x 4, alfa 0x80 (a escala do GS)
    uint64_t serial = 0;            // muda quando a imagem muda
    int64_t index = -1;             // o quadro (em ordem de exibição) desde o começo do filme
    uint32_t code = 0;              // MoviePlaying
    bool looping = false;           // LoopingMovie (o fundo de uma página) ou um filme de tela cheia
};
bool movie_image(MovieImage* out);

// O relatório e as medidas.
struct MovieStats {
    uint32_t started = 0;        // filmes abertos
    uint32_t finished = 0;       // filmes que chegaram ao fim (e laços)
    uint32_t stopped = 0;        // parados antes (pulados, a página mudou)
    uint32_t refused = 0;        // pedidos que não abriram (desligados, sem arquivo, sem decodificador)
    int64_t frame = -1;          // o quadro mostrado
    int64_t clock_frame = 0;     // o quadro do relógio
    int64_t pictures = 0;        // quadros do vídeo já vistos no arquivo (todos, no fim)
    int64_t decoded = 0;         // quadros decodificados
    uint64_t audio_consumed = 0; // amostras do filme tiradas pela SPU2
    uint64_t audio_starved = 0;  // amostras sem dados na entrada
    double decode_ms = 0.0;      // decodificação + conversão, total desde o último reset_stats
    double decode_ms_max = 0.0;  // o pior quadro
    uint64_t decode_calls = 0;   // chamadas de movie_image que decodificaram
};
const MovieStats& movie_stats();
void movie_reset_timing();
std::string movie_decoder_name();  // o MFT em uso ("-" sem nenhum; o motivo se nenhum serviu)
std::string movie_describe();      // a linha "filme ..." do relatório das capturas

// Testes: volta ao estado do boot (nada tocando, desligado, com decodificador).
void movie_debug_power_on();

}  // namespace nf
