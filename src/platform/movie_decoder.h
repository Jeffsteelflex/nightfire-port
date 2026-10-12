#pragma once
// O decodificador de MPEG-2 dos filmes no PC (só do port): o do Windows, pelo Media Foundation, sem nada baixado.
//
// No PS2 o IPU decodifica (a libmpeg da Sony, sceMpegGetPicture) e converte para RGBA32 (CSC). Aqui, um MFT de
// decodificação de vídeo do sistema (MFTEnumEx da categoria MFT_CATEGORY_VIDEO_DECODER para MFVideoFormat_MPEG2): na
// ordem do MFTEnumEx com MFT_ENUM_FLAG_SORTANDFILTER, o primeiro que ativa e aceita o vídeo. No Windows 11 desta máquina
// são dois, os dois da Microsoft: o "MPEG2VideoExtension" (o pacote da loja Microsoft.MPEG2VideoExtension, que costuma
// vir com o Windows) e o "Microsoft MPEG Video Decoder MFT" (msmpeg2vdec.dll, no SysWOW64 para os 32 bits), que aqui
// recusa a ativação com c004f011 (SL_E_PRODUCT_SKU_NOT_INSTALLED: o licenciamento do MPEG-2 do Windows). Sem nenhum,
// os filmes não tocam (o jogo segue como antes deles: psiMovieFinished verdadeiro).
//
// Entrada: as unidades de Mpeg2Units (platform/pss.h: um quadro com os cabeçalhos de antes), uma amostra cada. Saída:
// NV12 em ordem de exibição (o decodificador reordena os quadros B); a conversão para BGRA (nv12_to_bgra) é a do IPU,
// BT.601 de faixa limitada (Y 16-235), com o alfa 0x80 que o CSC do IPU escreve (a escala do GS: opaco).
// Sem GPU: o MFT decodifica na CPU (sem gerente do Direct3D); ~0.3 ms por quadro de 512x448 aqui.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct IMFTransform;

namespace nf {

struct MovieFrameNV12 {
    int width = 0, height = 0;
    std::vector<uint8_t> data;  // Y (width x height) e UV intercalados (width x height / 2), passo = width
};

// NV12 (passo = largura) para BGRA de 8 bits, BT.601 de faixa limitada; alfa = alpha (o CSC do IPU: 0x80).
void nv12_to_bgra(const uint8_t* nv12, int width, int height, uint8_t* bgra, uint8_t alpha = 0x80);
// Um pixel (testes): Y, Cb, Cr -> B, G, R.
void yuv_to_bgr(int y, int cb, int cr, uint8_t* bgr);

class Mpeg2Decoder {
public:
    Mpeg2Decoder() = default;
    ~Mpeg2Decoder();
    Mpeg2Decoder(const Mpeg2Decoder&) = delete;
    Mpeg2Decoder& operator=(const Mpeg2Decoder&) = delete;

    // Procura e liga o decodificador para um vídeo de width x height. false: nenhum MFT de MPEG-2 funciona.
    bool open(int width, int height, double fps);
    void close();
    bool ok() const { return mft_ != nullptr; }
    const std::string& name() const { return name_; }  // o nome do MFT em uso (o relatório)
    const std::string& failure() const { return failure_; }  // por que nenhum serviu

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
    void pull();
    bool set_output_nv12();

    IMFTransform* mft_ = nullptr;
    bool mf_started_ = false;
    bool com_ = false;
    std::string name_;
    std::string failure_;
    int width_ = 0, height_ = 0;
    int stride_ = 0;
    uint32_t out_size_ = 0;
    bool provides_samples_ = false;
    int64_t time_ = 0;
    int64_t duration_ = 0;
    std::deque<MovieFrameNV12> frames_;
    std::vector<MovieFrameNV12> spare_;
    uint64_t frames_out_ = 0;
};

}  // namespace nf
