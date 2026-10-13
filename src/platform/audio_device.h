#pragma once
// O aparelho de som do sistema (só do port; docs/port.md, "Plataforma: interfaces e backends"): uma fila de pedaços de
// PCM de 48 kHz, estéreo, 16 bits. platform/audio_out.h decide o que misturar e quando (a linha do tempo do jogo, o WAV
// de --audio-dump, o descarte com a fila cheia); o backend só toca a fila. No Windows é o XAudio2 2.9
// (audio_device_xaudio2.cpp); no macOS será o Core Audio (etapa 3).

#include <cstdint>
#include <memory>

namespace nf {

class AudioDevice {
public:
    virtual ~AudioDevice() = default;
    // Os pedaços entregues e ainda não tocados até o fim.
    virtual int queued_buffers() = 0;
    // Copia e enfileira frames estéreo (a fila guarda as cópias); false se o sistema recusou.
    virtual bool submit(const int16_t* pcm, int frames) = 0;
    // Começa a tocar (depois dos primeiros pedaços).
    virtual void start() = 0;
    // As amostras tocadas desde a criação (o relógio do que já saiu da caixa).
    virtual uint64_t played_frames() = 0;
};

// O aparelho padrão do sistema (nulo sem placa de som ou se o sistema recusar).
std::unique_ptr<AudioDevice> open_audio_device();

}  // namespace nf
