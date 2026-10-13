#pragma once
// O decodificador de MPEG-2 dos filmes no PC (só do port). Este cabeçalho não traz nada do sistema. Dois
// decodificadores atrás da mesma fachada (Mpeg2Decoder):
//   - o do sistema (open_system_mpeg2_decoder, um backend): no Windows o Media Foundation (movie_decoder_mf.cpp), sem
//     nada baixado; nas outras plataformas, por enquanto, nenhum (backends_null.cpp);
//   - o do port (platform/mpeg2_decoder.h): C++ padrão, escrito a partir da ISO/IEC 13818-2, em qualquer plataforma.
// A escolha (MovieDecoderChoice; [video] movie_decoder e --movie-decoder): auto (o padrão) usa o do sistema quando ele
// existe e serve, senão o do port; mf só o do sistema (sem ele os filmes não tocam, como antes do decodificador do port);
// own só o do port. No Windows com o Media Foundation funcionando nada muda; onde ele não ativa (sem a "MPEG-2 Video
// Extension" da loja) e no macOS os filmes tocam pelo do port.
//
// No PS2 o IPU decodifica (a libmpeg da Sony, sceMpegGetPicture) e converte para RGBA32 (CSC). No Windows, um MFT de
// decodificação de vídeo do sistema (MFTEnumEx da categoria MFT_CATEGORY_VIDEO_DECODER para MFVideoFormat_MPEG2): na
// ordem do MFTEnumEx com MFT_ENUM_FLAG_SORTANDFILTER, o primeiro que ativa e aceita o vídeo. No Windows 11 desta máquina
// são dois, os dois da Microsoft: o "MPEG2VideoExtension" (o pacote da loja Microsoft.MPEG2VideoExtension, que costuma
// vir com o Windows) e o "Microsoft MPEG Video Decoder MFT" (msmpeg2vdec.dll, no SysWOW64 para os 32 bits), que aqui
// recusa a ativação com c004f011 (SL_E_PRODUCT_SKU_NOT_INSTALLED: o licenciamento do MPEG-2 do Windows).
//
// Entrada: as unidades de Mpeg2Units (platform/pss.h: um quadro com os cabeçalhos de antes), uma por feed. Saída: NV12
// em ordem de exibição (os decodificadores reordenam os quadros B); a conversão para BGRA (nv12_to_bgra, aqui e sem
// nada do sistema) é a do IPU, BT.601 de faixa limitada (Y 16-235), com o alfa 0x80 que o CSC do IPU escreve (a escala
// do GS: opaco). Sem GPU: os dois decodificam na CPU.

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace nf {

struct MovieFrameNV12 {
    int width = 0, height = 0;
    std::vector<uint8_t> data;  // Y (width x height) e UV intercalados (width x height / 2), passo = width
};

// NV12 (passo = largura) para BGRA de 8 bits, BT.601 de faixa limitada; alfa = alpha (o CSC do IPU: 0x80).
void nv12_to_bgra(const uint8_t* nv12, int width, int height, uint8_t* bgra, uint8_t alpha = 0x80);
// Um pixel (testes): Y, Cb, Cr -> B, G, R.
void yuv_to_bgr(int y, int cb, int cr, uint8_t* bgr);

// Qual decodificador os filmes usam ([video] movie_decoder = auto|mf|own, --movie-decoder).
enum class MovieDecoderChoice : uint8_t {
    Auto,    // o do sistema se houver e servir, senão o do port
    System,  // só o do sistema ("mf" no .ini e na linha de comando)
    Own,     // só o do port
};
// "auto"; "mf" ou "system"; "own" ou "port" (sem diferenciar maiúsculas). false para o resto.
bool parse_movie_decoder_choice(const std::string& text, MovieDecoderChoice* out);
const char* movie_decoder_choice_name(MovieDecoderChoice c);  // "auto", "mf", "own"
// O nome do decodificador do port nos relatórios.
extern const char* const kOwnMpeg2DecoderName;

class Mpeg2Decoder {
public:
    // Um decodificador por trás da fachada: o do sistema (open_system_mpeg2_decoder) ou o do port.
    class Backend {
    public:
        virtual ~Backend() = default;
        virtual bool feed(Mpeg2Decoder& d, const uint8_t* data, size_t size) = 0;
        virtual void drain(Mpeg2Decoder& d) = 0;
        virtual void flush() = 0;

    protected:
        // Um quadro do reaproveitamento (o que take devolveu) e a entrega de um quadro pronto na fila.
        static MovieFrameNV12 reuse_frame(Mpeg2Decoder& d) { return d.reuse_frame(); }
        static void push_frame(Mpeg2Decoder& d, MovieFrameNV12&& f) { d.push_frame(std::move(f)); }
    };

    Mpeg2Decoder();
    ~Mpeg2Decoder();
    Mpeg2Decoder(const Mpeg2Decoder&) = delete;
    Mpeg2Decoder& operator=(const Mpeg2Decoder&) = delete;

    // Liga um decodificador para um vídeo de width x height pela escolha. false: nenhum serve (com mf, sem o do sistema).
    bool open(int width, int height, double fps, MovieDecoderChoice choice = MovieDecoderChoice::Auto);
    void close();
    bool ok() const { return backend_ != nullptr; }
    bool own() const { return own_; }                // o decodificador do port está em uso
    const std::string& name() const { return name_; }  // o nome do decodificador em uso (o relatório)
    // Por que o do sistema não serviu (com auto e o do port em uso, ou com mf e nenhum); vazio se ele serviu.
    const std::string& failure() const { return failure_; }

    // Uma unidade do vídeo elementar. Os quadros que saírem vão para a fila.
    bool feed(const uint8_t* data, size_t size);
    // Fim do stream: os quadros que o decodificador ainda guarda saem.
    void drain();
    // Descarta o que está dentro (outro filme, ou o laço) sem fechar.
    void flush();
    size_t frames_ready() const { return frames_.size(); }
    bool take(MovieFrameNV12& out);
    uint64_t frames_out() const { return frames_out_; }

private:
    MovieFrameNV12 reuse_frame();
    void push_frame(MovieFrameNV12&& f);

    std::unique_ptr<Backend> backend_;
    bool own_ = false;
    std::string name_;
    std::string failure_;
    std::deque<MovieFrameNV12> frames_;
    std::vector<MovieFrameNV12> spare_;
    uint64_t frames_out_ = 0;
};

// O decodificador de MPEG-2 do sistema (o backend: o Media Foundation em movie_decoder_mf.cpp; nenhum nos backends
// nulos). Nulo se não houver um que sirva, com o motivo em *failure; *name recebe o nome dele.
std::unique_ptr<Mpeg2Decoder::Backend> open_system_mpeg2_decoder(int width, int height, double fps, std::string* name,
                                                                std::string* failure);

}  // namespace nf
