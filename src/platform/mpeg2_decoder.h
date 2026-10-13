#pragma once
// Um decodificador de vídeo MPEG-2 do próprio port (ISO/IEC 13818-2), em C++ padrão, sem nada do sistema e sem nada do
// jogo: o que os filmes do PS2 (MOVIES/30_FPS/*.PSS) usam e o resto do perfil principal que custa pouco. Escrito a partir
// da norma (as tabelas do Anexo B, as contas da seção 7), sem código de outro decodificador. É o decodificador dos
// filmes onde o sistema não tem um (no Windows, o do Media Foundation continua o padrão; platform/movie_decoder.h).
//
// O que decodifica:
//   - MPEG-2 4:2:0 (perfis simples e principal; o tamanho que vier no cabeçalho de sequência): quadros I, P e B em
//     imagens de quadro (picture_structure 3), com previsão de quadro e de campo (frame_motion_type 1 e 2), DCT de quadro
//     e de campo (dct_type), varredura em zigue-zague e alternada, os dois VLC de intra (intra_vlc_format), as duas
//     escalas de quantização (q_scale_type), a precisão do DC de 8 a 11 bits, as matrizes do cabeçalho de sequência e da
//     extensão de matrizes, os vetores de ocultação (concealment_motion_vectors), macroblocos pulados;
//   - MPEG-1 (sem a extensão de sequência): os vetores em pixels inteiros (full_pel), o escape de 8/16 bits, a
//     quantização com o ajuste para ímpar no lugar do controle de descasamento, o enchimento de macroblocos.
// O que não decodifica (conta em Mpeg2Stats::unsupported e a imagem sai com o que a ocultação puser): imagens de campo
// (picture_structure 1 e 2), dual prime, 4:2:2 e 4:4:4, a escalabilidade e as imagens D do MPEG-1. Os 22 filmes do jogo
// só usam imagens de quadro progressivas, previsão de quadro, DCT de quadro, zigue-zague, intra_vlc_format 1,
// q_scale_type 1 e DC de 8 bits (docs/formats.md, "Vídeos").
//
// Entrada: o vídeo elementar em pedaços que terminam no fim de uma imagem (uma unidade de Mpeg2Units, platform/pss.h:
// os cabeçalhos e uma imagem com as fatias dela; várias imagens de uma vez também servem). Saída: as imagens na ordem de
// exibição, por uma função chamada durante decode()/finish() (a imagem aponta para os buffers do decodificador: copie
// antes de voltar). A reordenação é a da norma: uma imagem I ou P sai quando a próxima I ou P chega (ou no fim); uma B
// sai logo.
//
// As contas que têm mais de um jeito certo:
//   - a IDCT é separável, com um só arredondamento no fim: as linhas em inteiros de 32 bits (cos x 2^16, sem
//     arredondar), as colunas com cos x 2^20 em double (inteiros abaixo de 2^53: contas exatas, as mesmas em qualquer
//     CPU) e floor(x + 1/2), saturada em [-256, 255]. Fica a menos de 0.002 da fórmula exata, então cumpre com folga a
//     IEEE 1180 que o Anexo A pede (teste em tests/test_mpeg2.cpp); um bloco só com o DC sai com (DC + 4) >> 3, o
//     arredondamento exato. O Media Foundation arredonda diferente perto de k + 1/2 (docs/port.md, "Filmes");
//   - os erros de fluxo (um VLC que não existe, um macrobloco fora da imagem) acabam a fatia: os macroblocos que faltam
//     são copiados da referência mais recente (ou ficam cinza numa imagem I sem referência), e Mpeg2Stats::errors conta;
//   - SSE2 onde houver (a previsão, a soma dos blocos, as colunas da IDCT), com as mesmas contas do C++ puro: o mesmo
//     resultado bit a bit em qualquer CPU.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nf {

// Uma imagem decodificada, na ordem de exibição. Os ponteiros valem só durante a chamada da função de saída.
struct Mpeg2Picture {
    int width = 0, height = 0;                // horizontal_size e vertical_size (a área visível)
    int chroma_width = 0, chroma_height = 0;  // (width + 1) / 2, (height + 1) / 2 em 4:2:0
    const uint8_t* y = nullptr;
    const uint8_t* cb = nullptr;
    const uint8_t* cr = nullptr;
    int y_stride = 0, c_stride = 0;
    int coding_type = 0;          // 1 I, 2 P, 3 B
    int temporal_reference = 0;   // a posição de exibição dentro do GOP
    uint64_t decode_index = 0;    // a ordem de decodificação (0 = a primeira imagem desde o último reset)
    uint64_t display_index = 0;   // a ordem de saída (0 = a primeira que saiu)
    bool progressive_frame = true;
    bool top_field_first = false;
    int errors = 0;               // fatias com erro nesta imagem
};

struct Mpeg2Stats {
    uint64_t pictures = 0;      // imagens decodificadas
    uint64_t output = 0;        // imagens que saíram
    uint64_t slices = 0;
    uint64_t macroblocks = 0;   // decodificados (os pulados inclusive)
    uint64_t skipped = 0;       // macroblocos pulados
    uint64_t intra_blocks = 0;  // blocos 8x8 intra
    uint64_t coded_blocks = 0;  // blocos 8x8 não intra com coeficientes
    uint64_t errors = 0;        // fatias que acabaram num erro do fluxo
    uint64_t concealed = 0;     // macroblocos que não vieram e foram copiados (ocultação)
    uint64_t unsupported = 0;   // imagens ou macroblocos com um recurso que este decodificador não tem
    uint64_t dropped = 0;       // imagens P/B sem referência (o começo de um GOP aberto sem o anterior): não saem
};

class Mpeg2VideoDecoder {
public:
    using Output = std::function<void(const Mpeg2Picture&)>;

    Mpeg2VideoDecoder();
    explicit Mpeg2VideoDecoder(Output out);
    ~Mpeg2VideoDecoder();
    Mpeg2VideoDecoder(const Mpeg2VideoDecoder&) = delete;
    Mpeg2VideoDecoder& operator=(const Mpeg2VideoDecoder&) = delete;

    void set_output(Output out);
    // Bytes do vídeo elementar terminando no fim de uma imagem (ver o cabeçalho). As imagens que ficarem prontas para
    // a exibição saem pela função de saída antes de voltar.
    void decode(const uint8_t* data, size_t size);
    // Fim do stream: a última imagem de referência sai.
    void finish();
    // Esquece tudo (outro stream, o laço de um filme): nada sai; o próximo começa num cabeçalho de sequência.
    void reset();

    // O cabeçalho de sequência visto por último (0 antes do primeiro).
    int width() const;
    int height() const;
    double frame_rate() const;
    bool mpeg1() const;
    const Mpeg2Stats& stats() const;
    const std::string& last_error() const;  // a última coisa que deu errado (relatórios; vazio sem erro)

    // Depuração (nf_movie_check): a IDCT de referência em ponto flutuante no lugar da inteira, para separar o que é
    // arredondamento da IDCT do resto numa comparação.
    void set_reference_idct(bool on);

    struct State;

private:
    State* s_;
};

// Peças do decodificador expostas para os testes (tests/test_mpeg2.cpp). Nada aqui precisa ser usado fora deles.
namespace mpeg2 {

// A leitura de bits (MSB primeiro) sobre um buffer com pelo menos 4 bytes de folga depois do fim (zeros).
class BitReader {
public:
    BitReader() = default;
    BitReader(const uint8_t* data, size_t size_bytes) : p_(data), size_bits_(size_bytes * 8) {}
    uint32_t peek(int n) const {  // 1 <= n <= 25
        const uint8_t* b = p_ + (pos_ >> 3);
        const uint32_t w = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
        return (w << (pos_ & 7)) >> (32 - n);
    }
    void skip(int n) { pos_ += size_t(n); }
    uint32_t read(int n) {  // 1 <= n <= 25
        const uint32_t v = peek(n);
        skip(n);
        return v;
    }
    uint32_t read_long(int n) {  // até 32
        if (n <= 24) return read(n);
        const uint32_t hi = read(n - 16);
        return (hi << 16) | read(16);
    }
    size_t pos() const { return pos_; }
    size_t size_bits() const { return size_bits_; }
    bool overrun() const { return pos_ > size_bits_; }
    size_t left() const { return pos_ >= size_bits_ ? 0 : size_bits_ - pos_; }
    // Alinha no byte e procura o próximo código de início (0x000001xx); devolve false no fim.
    bool next_start_code();

private:
    const uint8_t* p_ = nullptr;
    size_t size_bits_ = 0;
    size_t pos_ = 0;
};

// Os VLC do Anexo B. Cada um lê do leitor; um código que não existe devolve kInvalid (e não avança).
constexpr int kInvalid = -1000;
constexpr int kMbEscape = 34;    // macroblock_escape (soma 33)
constexpr int kMbStuffing = 35;  // macroblock_stuffing (só no MPEG-1)
int read_mb_address_increment(BitReader& br);   // B.1: 1..33, kMbEscape, kMbStuffing
// B.2 a B.4: as flags de macroblock_type (picture_coding_type 1, 2 ou 3).
enum MbFlags : int { kMbQuant = 1, kMbForward = 2, kMbBackward = 4, kMbPattern = 8, kMbIntra = 16 };
int read_mb_type(BitReader& br, int picture_coding_type);
int read_coded_block_pattern(BitReader& br);    // B.9 (4:2:0): 0..63
int read_motion_code(BitReader& br);            // B.10: -16..16
int read_dmvector(BitReader& br);               // B.11: -1..1
int read_dc_size(BitReader& br, bool chroma);   // B.12 / B.13: 0..11
// B.14 (table 0) e B.15 (table 1): um coeficiente. Devolve 1 (run/level), 0 no fim do bloco, kInvalid num código que não
// existe. first: o primeiro coeficiente de um bloco não intra (o código '1s' da B.14). mpeg1: o escape de 8/16 bits.
int read_dct_coefficient(BitReader& br, int table, bool first, bool mpeg1, int* run, int* level);

// As varreduras (posição na varredura -> índice em ordem de linhas) e as tabelas da norma.
extern const uint8_t kZigzag[64];
extern const uint8_t kAlternateScan[64];
extern const uint8_t kDefaultIntraMatrix[64];  // em ordem de linhas
extern const uint8_t kNonLinearQuantiserScale[32];
int quantiser_scale(int quantiser_scale_code, bool q_scale_type);

// A quantização inversa de um bloco (7.4.2 a 7.4.4): qf em ordem de linhas (o DC de um bloco intra já com o preditor
// somado), w a matriz em ordem de linhas; saída f em ordem de linhas, saturada, com o controle de descasamento (MPEG-2)
// ou o ajuste para ímpar (MPEG-1).
void inverse_quantise(const int16_t qf[64], const uint8_t w[64], int quantiser_scale, bool intra, int intra_dc_precision,
                      bool mpeg1, int16_t f[64]);

// A IDCT do decodificador, no lugar (entrada em [-2048, 2047], saída em [-256, 255]).
void idct(int16_t block[64]);
// A mesma em C++ puro (a das CPUs sem SSE2; nas com SSE2, idct faz as colunas em vetores, com o mesmo resultado).
void idct_portable(int16_t block[64]);
// A previsão com meio pixel (7.6.4): um bloco w x h de ref (passo ref_stride) na posição (x, y) de pixels inteiros mais
// os meios (half_x, half_y), em dst (passo dst_stride). average: a média com o que dst já tem (a previsão bidirecional).
void predict(const uint8_t* ref, int ref_stride, int x, int y, int half_x, int half_y, int w, int h, uint8_t* dst,
             int dst_stride, bool average);
// A mesma em C++ puro (w 16 ou 8): a das CPUs sem SSE2; nas com SSE2, predict usa a versão vetorial, com o mesmo
// resultado.
void predict_portable(const uint8_t* ref, int ref_stride, int x, int y, int half_x, int half_y, int w, int h,
                      uint8_t* dst, int dst_stride, bool average);

}  // namespace mpeg2

}  // namespace nf
