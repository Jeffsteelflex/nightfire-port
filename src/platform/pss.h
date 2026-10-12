#pragma once
// Os filmes do PS2 (MOVIES/30_FPS/*.PSS) no PC, a parte sem decodificador nem GPU (só do port; testável). Formato em
// docs/formats.md, "Vídeos".
//
// Um PSS é um program stream MPEG-2: pacotes 0x000001BA de 16 KB com PES de vídeo (stream 0xE0, MPEG-2 512x448 a
// 30 fps) e de áudio no private stream 1 (0xBD: 4 bytes de cabeçalho, 0xff 0xa0 0 0, e o áudio no formato "SS" da
// Sony: o cabeçalho SShd/SSbd e PCM de 16 bits little-endian, 48 kHz, estéreo, em blocos de 0x200 bytes alternados
// por canal). Aqui:
//   - pss_next: um passo do demultiplexador sobre um buffer (um PES, ou um cabeçalho de pacote/sistema/enchimento
//     pulado, ou o fim, ou "faltam bytes");
//   - PssAudio: junta o áudio dos PES, lê o cabeçalho SShd/SSbd e desintercala os blocos em amostras estéreo, o que a
//     entrada de dados da SPU2 faz no PS2 (os blocos são o formato da transferência em bloco do núcleo 0);
//   - Mpeg2Units: divide o vídeo elementar em unidades para o decodificador (cada quadro com os cabeçalhos de sequência
//     e de GOP que vêm antes dele), contando os quadros;
//   - PssFile: lê um arquivo aos pedaços e alimenta os dois (o áudio fica numa fila de amostras, o vídeo numa fila de
//     unidades), até o quanto for pedido;
//   - as contas do relógio do filme (movie_*): o quadro do vídeo pelo áudio já ouvido (a sincronia do PC: o vídeo
//     segue o relógio do som; no PS2 o vídeo anda um quadro por tick de 2 VBlanks e o som pelo relógio da SPU2, sem
//     sincronia), pelos VBlanks quando o filme não tem som (os fundos do menu) ou depois que o som acabou.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "platform/disc_source.h"

namespace nf {

// ---- o program stream ----

struct PssPes {
    uint8_t stream = 0;     // 0xE0-0xEF vídeo, 0xC0-0xDF áudio MPEG, 0xBD private stream 1
    bool has_pts = false;
    uint64_t pts = 0;       // 90 kHz
    const uint8_t* data = nullptr;  // o payload (sem o cabeçalho do PES)
    size_t size = 0;
};

enum class PssStep : uint8_t {
    Pes,       // *out tem um PES (vídeo, áudio ou private stream 1)
    Other,     // cabeçalho de pacote, de sistema, enchimento ou outro stream: pulado
    End,       // 0x000001B9
    NeedMore,  // o próximo item não cabe no que há: leia mais (ou é o fim do arquivo)
    Error,     // sem código de início onde devia haver
};

// Um item do program stream no começo de data. *used = bytes consumidos (0 com NeedMore/Error).
PssStep pss_next(const uint8_t* data, size_t size, size_t* used, PssPes* out);

// ---- o áudio "SS" (SShd/SSbd) ----

class PssAudio {
public:
    static constexpr size_t kHeaderBytes = 0x28;  // SShd (8 + 0x18) + SSbd (8): o que audioDecBeginPut junta antes

    // O payload de um PES 0xBD inteiro (com os 4 bytes: 0xff, o sub-stream, 0, 0). Só o sub-stream 0xa0 conta.
    void push_pes(const uint8_t* payload, size_t size);
    // Os bytes do áudio já sem os 4 bytes de cada PES (testes).
    void push_bytes(const uint8_t* data, size_t size);
    void reset();

    bool header_ok() const { return header_ok_; }
    bool error() const { return error_; }  // o cabeçalho não é SShd/SSbd, ou o tipo não é PCM de 16 bits
    int type() const { return type_; }
    int rate() const { return rate_; }
    int channels() const { return channels_; }
    int interleave() const { return interleave_; }
    uint32_t body_size() const { return body_size_; }

    // Amostras estéreo prontas (pares esquerdo, direito), na ordem.
    size_t frames_ready() const { return (pcm_.size() - pcm_read_) / 2; }
    size_t take(int16_t* out, size_t max_frames);  // copia e consome; devolve quantas
    uint64_t frames_decoded() const { return decoded_; }  // todas as que já saíram dos blocos

private:
    void parse();

    std::vector<uint8_t> bytes_;  // ainda não usados (o cabeçalho, ou um par de blocos incompleto)
    size_t bytes_read_ = 0;
    std::vector<int16_t> pcm_;    // amostras estéreo intercaladas
    size_t pcm_read_ = 0;
    bool header_ok_ = false;
    bool error_ = false;
    int type_ = 0, rate_ = 0, channels_ = 0, interleave_ = 0;
    uint32_t body_size_ = 0;
    uint32_t body_used_ = 0;
    uint64_t decoded_ = 0;
};

// ---- o vídeo elementar ----

class Mpeg2Units {
public:
    void push(const uint8_t* data, size_t size);
    // Fim do stream: o resto vira a última unidade.
    void finish();
    void reset();
    // A próxima unidade completa (um quadro, com os cabeçalhos de sequência e de GOP de antes dele).
    bool next(std::vector<uint8_t>& unit);
    size_t units_ready() const { return units_.size(); }
    uint32_t pictures() const { return pictures_; }  // quadros já vistos (códigos 0x00000100)
    bool finished() const { return finished_; }
    // O cabeçalho de sequência (0xB3): largura, altura, código de aspecto e de taxa; 0 antes de ver um.
    int width() const { return width_; }
    int height() const { return height_; }
    int aspect_code() const { return aspect_; }
    int rate_code() const { return rate_code_; }
    double fps() const;

private:
    void scan();

    std::vector<uint8_t> buf_;
    size_t scanned_ = 0;       // até onde os códigos de início já foram procurados
    size_t unit_start_ = 0;    // começo da unidade em montagem
    bool in_picture_ = false;  // a unidade em montagem já tem um quadro
    std::deque<std::vector<uint8_t>> units_;
    uint32_t pictures_ = 0;
    bool finished_ = false;
    int width_ = 0, height_ = 0, aspect_ = 0, rate_code_ = 0;
};

// ---- o arquivo ----

class PssFile {
public:
    PssFile() = default;
    PssFile(const PssFile&) = delete;
    PssFile& operator=(const PssFile&) = delete;
    ~PssFile();
    // Um arquivo do disco (platform/disc_source.h: da pasta, da imagem ISO, do leitor ou da cópia pessoal) ou do PC.
    bool open(DiscFile file);
    bool open(const std::wstring& path);
    void close();
    bool is_open() const { return file_.is_open(); }
    // Volta ao começo (o laço dos fundos do menu: o PS2 fecha e reabre o arquivo).
    bool rewind();

    // Lê até haver pelo menos audio_frames amostras de áudio prontas e video_units unidades de vídeo (ou o fim do
    // arquivo, ou max_bytes lidos nesta chamada). Devolve falso num erro do arquivo.
    bool pump(size_t audio_frames, size_t video_units, size_t max_bytes = SIZE_MAX);
    bool eof() const { return eof_; }
    bool error() const { return error_; }
    PssAudio& audio() { return audio_; }
    Mpeg2Units& video() { return video_; }
    bool has_audio() const { return audio_pes_ > 0; }
    uint64_t bytes_read() const { return bytes_read_; }

private:
    bool read_more();

    DiscFile file_;
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
    bool eof_ = false;
    bool error_ = false;
    uint64_t bytes_read_ = 0;
    uint32_t audio_pes_ = 0;
    PssAudio audio_;
    Mpeg2Units video_;
};

// ---- o relógio do filme ----

// O áudio do filme que já se ouviu: o que a SPU2 consumiu da entrada menos o que ainda está na fila da saída (o
// XAudio2; 0 sem aparelho). Nunca negativo.
int64_t movie_audio_heard(int64_t consumed_frames, int64_t queued_frames);
// O quadro (em ordem de exibição) para um tempo em amostras de áudio: floor(amostras x fps / taxa).
int64_t movie_frame_at_audio(int64_t audio_frames, int rate, double fps);
// O quadro para um tempo em VBlanks de 60 Hz: floor(vblanks x fps / 60).
int64_t movie_frame_at_vblanks(double vblanks, double fps);
// O relógio de um filme, em amostras de 48 kHz desde o primeiro quadro: o som do filme ouvido enquanto ele dura; sem
// som, ou depois que ele acaba (os últimos quadros de um vídeo um pouco mais longo que o áudio), o relógio da própria
// SPU2 (as amostras que ela misturou e já se ouviram; 800 por VBlank) desde o começo ou desde o fim do som. Os dois
// andam pelo que sai da caixa, então o vídeo segue o som também nos filmes mudos.
struct MovieClock {
    bool has_audio = false;
    bool audio_done = false;     // o arquivo acabou e a entrada da SPU2 esvaziou
    int64_t audio_end = 0;       // o som do filme ouvido quando ele acabou
    int64_t spu_start = 0;       // o relógio da SPU2 (ouvido) no começo do filme
    int64_t spu_audio_end = 0;   // o relógio da SPU2 quando o som acabou
    // A posição em amostras de 48 kHz: heard_audio = o som do filme ouvido, heard_spu = o relógio da SPU2 ouvido.
    int64_t position(int64_t heard_audio, int64_t heard_spu) const;
};

}  // namespace nf
