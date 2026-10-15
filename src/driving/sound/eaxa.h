#pragma once
// O decodificador de EA-XA do SND da EA (libsndps2z.a, sdecmut.o/stretch.o), portado do MIPS do DRIVING.ELF e conferido
// contra o original no nf_driving_check (parte 13, `--so som`). docs/driving.md seção 17.
//
// EA-XA é o ADPCM da EA dos fluxos de áudio (.asf dentro dos .MUS e .SPE: música e falas, 48 kHz, codec 0x0a do PT):
// blocos de 15 bytes por canal, 28 amostras cada. O byte 0 é o filtro (nibble alto: a linha das tabelas de
// coeficientes, 0x00360bb8 e 0x00360bc8) e o deslocamento (nibble baixo); os 14 seguintes são 28 nibbles com sinal,
// o alto primeiro. Cada amostra é s = ((n << (12 - shift)) + s1 x k0) + s2 x k1, em float com a FPU do EE: o
// SND::decodexac (0x00318900) monta a conta no acumulador da FPU (ADDA.S n, 0; MADDA.S s1, k0; MADD.S s2, k1), cada
// produto e cada soma truncados (platform/ps2_float.h). A saída é float, sem saturação (o mixador do SND converte
// depois).
//
// O pacote de um canal (o que o SNDPKTPLAY entrega ao SFILTER_unpackxapf): dois s16 (o estado: a última amostra e a de
// antes, que o unpackxapf converte com cvt.s.w e passa ao SetState) e os blocos.
//
// Divergência (guarda): um filtro de 4 a 15 lê no PS2 o que vem depois das tabelas no .data (outras variáveis); aqui
// esses filtros usam os coeficientes 0 e contam em eaxa_guards(). Nenhum fluxo do disco os tem (a parte 13 conta).

#include <cstdint>

namespace drv {

// SND::MXAPACKETF (o pedido do decodexac): +0 amostras a fazer (múltiplo de 28 ou o resto; fica negativo depois de um
// bloco parcial), +4/+8 o estado (float: a última amostra e a de antes), +0xc a entrada, +0x10 a saída.
struct MxaPacketF {
    int32_t count = 0;
    float s1 = 0.0f;
    float s2 = 0.0f;
    const uint8_t* in = nullptr;
    float* out = nullptr;
};

// [driving] 0x00318900 SND::decodexac(SND::MXAPACKETF *)
void SND_decodexac(MxaPacketF& p);

// SND::CEAXABLKDecf (0xa8 bytes no PS2; aqui com ponteiros nativos): um canal de um fluxo, alimentado por pacotes.
struct CEAXABLKDecf {
    int32_t left = 0;        // +0x04 amostras ainda no buffer interno (de um bloco decodificado pela metade)
    int32_t frames = 0;      // +0x08 amostras que faltam do pacote alimentado
    int32_t bytes = 0;       // +0x0c (o Feed guarda; ninguém lê)
    const float* next = nullptr;  // +0x10 a próxima amostra do buffer interno
    float buffer[28] = {};   // +0x24 o último bloco decodificado pela metade
    MxaPacketF pkt;          // +0x94 o pedido do decodexac (o estado em +0x98/+0x9c, a entrada em +0xa0)
};

// [driving] 0x00318d90 SND::CEAXABLKDecf::CEAXABLKDecf(void)
void CEAXABLKDecf_Construct(CEAXABLKDecf& d);
// [driving] 0x00318db0 SND::CEAXABLKDecf::Feed(void *, int, int): 0 se aceitou (só com o pacote anterior acabado).
int32_t CEAXABLKDecf_Feed(CEAXABLKDecf& d, const uint8_t* data, int32_t bytes, int32_t frames);
// [driving] 0x00318dd8 SND::CEAXABLKDecf::Decode(float **, int): até n amostras em *out (o ponteiro não anda); devolve
// quantas.
int32_t CEAXABLKDecf_Decode(CEAXABLKDecf& d, float* const* out, int32_t n);
// [driving] 0x00319018 SND::CEAXABLKDecf::SetState(SND::XAFSTATE *)
void CEAXABLKDecf_SetState(CEAXABLKDecf& d, float s1, float s2);

// Filtros 4 a 15 vistos (a guarda do cabeçalho).
uint64_t eaxa_guards();
void eaxa_reset_guards();

}  // namespace drv
