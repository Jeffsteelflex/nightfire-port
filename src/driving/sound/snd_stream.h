#pragma once
// Os fluxos do SND no EE (SNDSTRM_*, SNDSTRMI_*, SNDPKTPLAY*, SNDLINKI_*, SNDI_patchtohdr), portados do MIPS do
// DRIVING.ELF e conferidos contra o original na parte 23 do nf_driving_check (docs/driving.md seção 27).
//
// Um fluxo (SNDSTRMI, 0x138 bytes, na memória que o AStreamPriv dá ao SNDSTRM_create; os pedidos de 0x28 bytes e o
// tocador de pacotes vêm logo depois, e o STREAM no fim) tem uma fila de pedidos (cada SNDSTRM_queuefile: um arquivo
// .asf), lidos pedaço a pedaço do STREAM (driving/sound/stream.h) pelo cliente do servidor de 100 Hz
// (SNDSTRMI_service): o SCHl (o cabeçalho PT: SNDI_patchtohdr dá o formato e os atributos; se mudaram, o fluxo espera
// as amostras na fila acabarem e começa outra voz) e os SCDl (cada um um pacote de EA-XA dos canais, entregue ao
// tocador de pacotes: SNDPKTPLAY_submit). O tocador (SNDPKTPLAY, 0x5c bytes + 0x20 por pacote) aloca uma voz do SND de
// prioridade 101 no mixador do EE (SNDPKTPLAY_start, SNDPLATFORM_packetplay) e o desempacotador do mixador puxa os
// pacotes (SNDPKTPLAYI_get); as amostras tocadas voltam ao fluxo (SNDPKTPLAYI_freeframes, o callback de quadros: a
// contagem do pedido) e os pacotes acabados ao STREAM (o callback de liberação), pela fila que o driver esvazia
// depois de cada mistura (SNDPKTPLAYI_flushcallbackdata).
//
// Um pedido "segurado" (hold < 0) não começa até o SNDSTRM_modifyhold; com hold > 0 espera ter tantos milissegundos no
// buffer (com a leitura instantânea do PC, o buffer enche na hora).

#include <cstdint>

#include "driving/sound/snd.h"
#include "platform/ptr32.h"

namespace drv {

// SNDSAMPLEFORMAT (4 bytes).
struct SndSampleFormat {
    uint16_t rate;              // +0x00
    uint8_t channels;           // +0x02
    uint8_t codec;              // +0x03 5 PS-ADPCM, 10 EA-XA
};
static_assert(sizeof(SndSampleFormat) == 4);

// SNDSAMPLEATTRIBUTES (0x68): o que o SNDI_patchtohdr tira das etiquetas.
struct SndSampleAttr {
    uint16_t w00;               // +0x00 (etiqueta 0x80 no byte +7; o +0 fica 0)
    uint8_t priority;           // +0x02 etiqueta 6
    uint8_t vol;                // +0x03 0x7f
    uint8_t pan;                // +0x04 0x40
    uint8_t fx;                 // +0x05 etiqueta 0x13
    int8_t bend_range;          // +0x06 etiqueta 0x0a
    uint8_t t80;                // +0x07 etiqueta 0x80
    uint16_t render;            // +0x08 o modo de render (etiqueta 0x8c, ou as bandeiras da 0xa1)
    uint16_t pad0a;
    uint16_t azimuth[6];        // +0x0c
    nf::P32<uint8_t> stretch[6];  // +0x18 etiquetas 0x98..0x9b, 0xa4, 0xa5 (cópias no heap do SND)
    uint32_t stretch_size[6];   // +0x30
    nf::P32<uint8_t> user[4];   // +0x48 etiqueta 0x14 (os dados do usuário)
    uint32_t user_size[4];      // +0x58
};
static_assert(sizeof(SndSampleAttr) == 0x68);

// Um pedido de um fluxo (0x28; o nó do SNDLINKI no começo).
struct SndStreamRequest {
    nf::P32<SndStreamRequest> next;  // +0x00
    nf::P32<SndStreamRequest> prev;  // +0x04
    uint32_t stream_req;        // +0x08 o pedido do STREAM
    int32_t id;                 // +0x0c a geração (+0x100) | o índice do fluxo
    uint32_t rate;              // +0x10 bytes por segundo (SNDSTRMI_calcdatarate; 0 até o cabeçalho)
    uint32_t played;            // +0x14 amostras tocadas
    uint32_t total;             // +0x18 amostras do arquivo (etiqueta 0x85)
    uint32_t outstanding;       // +0x1c entregues ao tocador e ainda não tocadas
    int32_t hold;               // +0x20 ms a ter no buffer antes de começar; < 0 segura até o modifyhold
    uint8_t started;            // +0x24
    uint8_t pad25[3];
};
static_assert(sizeof(SndStreamRequest) == 0x28);

// SNDLINKLIST (0xc).
struct SndLinkList {
    nf::P32<SndStreamRequest> head;  // +0x00
    nf::P32<SndStreamRequest> tail;  // +0x04
    int32_t count;              // +0x08
};
static_assert(sizeof(SndLinkList) == 0xc);

// SNDSTREAMCHANNEL (0x138).
struct SndStreamChannel {
    uint32_t stream;            // +0x00 o STREAM (o que o STREAM_create devolveu)
    int32_t voice;              // +0x04 a voz do SND (-1 nenhuma)
    int32_t player;             // +0x08 o SNDPKTPLAY
    int32_t gen;                // +0x0c
    uint8_t state;              // +0x10 0 parado, 1 tocando, 2 o formato mudou (espera a fila acabar)
    uint8_t tap;                // +0x11 1: o STREAM é de outro (SNDSTRM_createtap)
    uint8_t pad12[2];
    SndSampleFormat fmt;        // +0x14 o que toca
    SndSampleFormat fmt_new;    // +0x18 o do último cabeçalho
    SndSampleAttr attr;         // +0x1c
    SndSampleAttr attr_new;     // +0x84
    SndPlayOpts opts;           // +0xec
    uint8_t filter[0x18];       // +0x104 (o +0x10, 0x114: um filtro pedido)
    SndLinkList active;         // +0x11c os pedidos na fila (o primeiro toca)
    SndLinkList free_list;      // +0x128
    nf::P32<SndStreamRequest> header_req;  // +0x134 o pedido do último cabeçalho
};
static_assert(sizeof(SndStreamChannel) == 0x138);

// O tocador de pacotes (SNDPKTPLAY: 0x5c + 0x20 por pacote).
struct SndPacket {               // 0x20
    int32_t seq;                // +0x00
    uint32_t frames;            // +0x04 bit 31: continuação (não é o primeiro do pedido)
    nf::P32<const uint8_t> data[6];  // +0x08 os dados de cada canal
};
static_assert(sizeof(SndPacket) == 0x20);
struct SndPacketPlayer {
    int32_t voice;              // +0x00 o identificador da voz do SND (-1)
    int32_t seq;                // +0x04
    uint16_t rd[6];             // +0x08 o próximo pacote de cada canal
    int16_t queued[6];          // +0x14 os pacotes na fila de cada canal
    uint16_t slots;             // +0x20
    uint8_t high;               // +0x22 o canal que conta as amostras (SNDPKTPLAYI_gethighchannel)
    uint8_t active;             // +0x23
    uint16_t release_rd;        // +0x24 o próximo pacote a liberar
    uint16_t wr;                // +0x26
    int32_t frames_queued;      // +0x28
    int32_t frames_out;         // +0x2c entregues ao mixador e não contadas
    nf::P32<uint8_t> mem;       // +0x30
    uint32_t release_cb;        // +0x34 o endereço do PS2 (0x00308978 SNDSTRMI_releasecallback)
    uint32_t frames_cb;         // +0x38 (0x003089a0 SNDSTRMI_framescallback)
    nf::P32<SndStreamChannel> context;  // +0x3c
    SndSampleFormat fmt;        // +0x40
    uint32_t stretch[6];        // +0x44
    SndPacket packets[1];       // +0x5c
};
static_assert(sizeof(SndPacketPlayer) == 0x7c);

// SNDSTREAMSTATUS e SNDREQUESTSTATUS (o AStream lê os dois a cada quadro).
struct SndStreamStatus {
    int32_t requests;           // +0x00 na fila
    int32_t current;            // +0x04 o identificador do que toca
    int32_t buffered_ms;        // +0x08
};
struct SndRequestStatus {
    int32_t state;              // +0x00 0 na fila, 1 começou, 2 tocando, 3 acabou ou não existe
    int32_t played_ms;          // +0x04
    int32_t left_ms;            // +0x08
    int32_t outstanding_ms;     // +0x0c
};

// [driving] 0x003099d0 SNDSTRM_overheadtap
int32_t SNDSTRM_overheadtap(int32_t requests, int32_t packets);
// [driving] 0x00309a08 SNDSTRM_overhead
int32_t SNDSTRM_overhead(int32_t requests, int32_t packets);
// [driving] 0x00309700 SNDSTRM_create (o SNDSTRMI_create 0x00309318 com tap = 0)
int32_t SNDSTRM_create(const SndPlayOpts* opts, int32_t requests, int32_t packets, uint8_t* mem, int32_t size);
// [driving] 0x00309720 SNDSTRM_destroy
int32_t SNDSTRM_destroy(int32_t stream);
// [driving] 0x003097c0 SNDSTRM_queuefile (o SNDSTRMI_queue 0x003095d0 com o tipo 0)
int32_t SNDSTRM_queuefile(int32_t stream, int32_t hold, const char* name, int32_t offset);
// [driving] 0x003097e0 SNDSTRM_purge
int32_t SNDSTRM_purge(int32_t stream);
// [driving] 0x00309a50 SNDSTRM_status
int32_t SNDSTRM_status(int32_t stream, SndStreamStatus* st);
// [driving] 0x00309b48 SNDSTRM_requeststatus
int32_t SNDSTRM_requeststatus(int32_t id, SndRequestStatus* st);
// [driving] 0x00309c88 SNDSTRM_modifyhold
int32_t SNDSTRM_modifyhold(int32_t id, int16_t hold);
// [driving] 0x00309d50 SNDSTRM_vol
int32_t SNDSTRM_vol(int32_t stream, int32_t vol);
// [driving] 0x00309d98 SNDSTRM_autovol
int32_t SNDSTRM_autovol(int32_t stream, int32_t ms, int32_t vol);
// [driving] 0x00309e00 SNDSTRM_3dpos
int32_t SNDSTRM_3dpos(int32_t stream, int32_t azimuth, int32_t elevation);
// [driving] 0x00309e60 SNDSTRM_pitchmult
int32_t SNDSTRM_pitchmult(int32_t stream, int32_t mult);
// [driving] 0x00309ea8 SNDSTRM_lowpass
int32_t SNDSTRM_lowpass(int32_t stream, int32_t cutoff);
// [driving] 0x00309ef0 SNDSTRM_setgreedylevel
int32_t SNDSTRM_setgreedylevel(int32_t stream, int32_t level);
// [driving] 0x00309088 SNDSTRMI_service (o cliente do servidor de 100 Hz)
void SNDSTRMI_service();

// O tocador de pacotes (o mixador e o driver os chamam).
// [driving] 0x00311e08 SNDPKTPLAYI_get: os dados do canal no próximo pacote (nulo sem pacote).
const int16_t* SNDPKTPLAYI_get(int32_t player, int32_t chan, int32_t* frames, int32_t* cont);
// [driving] 0x00312068 SNDPKTPLAYI_freeframes
void SNDPKTPLAYI_freeframes(int32_t player, int32_t chan, int32_t frames);
// [driving] 0x003120d8 SNDPKTPLAYI_flushcallbackdata
void SNDPKTPLAYI_flushcallbackdata();
// [driving] 0x003192b8 SNDPKTPLAYI_voicetopackethandle
int32_t SNDPKTPLAYI_voicetopackethandle(int32_t voice);
// [driving] 0x00319030 SNDDRV_getmastervoice (o índice do mixador do EE)
int32_t SNDDRV_getmastervoice(int32_t mv);
// [driving] 0x00319068 SNDDRV_getsamplechan
int32_t SNDDRV_getsamplechan(int32_t mv);
// [driving] 0x00319100 SNDPLATFORM_packetplay (driving/sound/snd_ps2.cpp)
int32_t SNDPLATFORM_packetplay(int32_t player, int32_t v, int32_t time_mult, int32_t distort, int32_t lowpass,
                               int32_t highpass, const SndSampleFormat* fmt, const nf::P32<uint8_t>* stretch);

// [driving] 0x0030c0f8 SNDI_patchtohdr (exposto para a conferência): o formato, os atributos e a informação ([0] as
// amostras, [1..6] os deslocamentos dos canais) do cabeçalho PT de um fluxo.
void snd_test_patchtohdr(uint32_t base, const uint8_t* patch, SndSampleFormat* fmt, SndSampleAttr* attr,
                         uint32_t* info);
// Volta ao estado do boot (as tabelas de 0x00357160 e 0x0035e078..0x0035e37c).
void snd_stream_power_on();
// Para a conferência e os relatórios.
SndStreamChannel* snd_stream_channel(int32_t stream);
SndPacketPlayer* snd_packet_player(int32_t player);
int32_t snd_callback_queue_count();
uint64_t snd_stream_guards();

}  // namespace drv
