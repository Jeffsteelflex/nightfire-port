#pragma once
// A saída de áudio do PC (só do port): a SPU2 (platform/spu2.h) misturada no ritmo dos VBlanks do jogo.
//
// O PS2 toca a SPU em tempo real, independente do jogo. Aqui a mistura anda em degraus: a cada lote mandado ao driver
// do IOP (sfx_iop_rpc, uma vez por VBlank em que o jogo roda SFXUpdate), audio_out_catch_up mistura os 800 frames
// (48 kHz / 60) de cada VBlank passado desde o último lote e os entrega ao destino:
//   - Off: a mistura acontece igual (as vozes andam, as falas acabam, ENVX muda como no PS2) e o som é descartado.
//     É o padrão dos testes, do nf_loader_check e das capturas (--screenshot), que ficam mudos e determinísticos.
//   - Device: XAudio2 2.9 (do SDK do Windows), uma voz de 48 kHz estéreo de 16 bits; a fila guarda ~4 VBlanks
//     (67 ms) e o excesso é descartado (depois de uma carga longa só o fim do que a SPU tocou é ouvido).
//   - Dump: grava tudo num WAV (--audio-dump), na mesma linha do tempo do jogo, para medir sem caixas de som.
// Como a quantidade misturada depende só dos VBlanks, o resultado não depende do aparelho: um dump e o jogo com som
// fazem as mesmas vozes.

#include <cstdint>
#include <string>

namespace nf {

enum class AudioMode { Off, Device, Dump };

// Abre o destino (fecha o anterior). Device falha sem placa de som (volta Off); Dump falha sem poder criar o arquivo.
bool audio_out_open_device();
bool audio_out_open_dump(const std::wstring& path);
void audio_out_close();  // fecha o WAV (acerta os tamanhos) e o XAudio2
AudioMode audio_out_mode();

// Mistura até o VBlank dado (chamado por PS2_SFX_SendBatchCommandsToIOP antes de cada lote). A primeira chamada só
// marca o começo. Lacunas maiores que 10 s são encurtadas (o resto não é misturado).
void audio_out_catch_up(uint32_t vblank);
// Os VBlanks já misturados desde o começo (o relógio do driver do IOP, psiSFXGetTimer).
uint64_t audio_out_vblanks();
// O que já foi misturado mas ainda não se ouviu, em amostras de 48 kHz: com o aparelho, o que está na fila do XAudio2
// (entregue menos SamplesPlayed); sem ele, 0. O relógio dos filmes desconta isto (platform/psi_movie.h: o vídeo segue o
// som que sai da caixa).
int64_t audio_out_queued_frames();

// Para o relatório: o que saiu nos últimos 0.5 s (pico e RMS em dBFS), frames totais, descartes e faltas na fila.
struct AudioOutStats {
    uint64_t frames = 0;
    uint32_t dropped_chunks = 0;  // Device: pedaços descartados por fila cheia
    uint32_t starved = 0;         // Device: lotes que acharam a fila vazia
    float peak_db = -120.0f;
    float rms_db = -120.0f;
};
AudioOutStats audio_out_stats();

// Testes: volta ao estado inicial (Off, contadores zerados).
void audio_out_debug_power_on();

}  // namespace nf
