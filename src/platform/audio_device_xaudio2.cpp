// O aparelho de som no Windows: o XAudio2 2.9 do SDK do Windows (xaudio2.lib), uma voz de 48 kHz estéreo de 16 bits.
// Ver audio_device.h e audio_out.h.

#include "platform/audio_device.h"

#include <windows.h>
#include <objbase.h>
#include <xaudio2.h>

#include <vector>

namespace nf {

namespace {

constexpr int kChunks = 16;  // buffers na fila do XAudio2 (audio_out.cpp não deixa passar de 8 na fila)

class XAudio2Device final : public AudioDevice {
public:
    ~XAudio2Device() override {
        if (source_ != nullptr) {
            source_->Stop(0);
            source_->DestroyVoice();
            source_ = nullptr;
        }
        if (master_ != nullptr) {
            master_->DestroyVoice();
            master_ = nullptr;
        }
        if (xa_ != nullptr) {
            xa_->Release();
            xa_ = nullptr;
        }
        if (com_) {
            CoUninitialize();
            com_ = false;
        }
    }

    bool open() {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        com_ = SUCCEEDED(co);
        if (FAILED(XAudio2Create(&xa_, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
            xa_ = nullptr;
            return false;
        }
        if (FAILED(xa_->CreateMasteringVoice(&master_))) return false;
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 2;
        fmt.nSamplesPerSec = 48000;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 4;
        fmt.nAvgBytesPerSec = 48000 * 4;
        return SUCCEEDED(xa_->CreateSourceVoice(&source_, &fmt));
    }

    int queued_buffers() override {
        XAUDIO2_VOICE_STATE st{};
        source_->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        return int(st.BuffersQueued);
    }

    bool submit(const int16_t* pcm, int frames) override {
        std::vector<int16_t>& buf = ring_[ring_next_];
        ring_next_ = (ring_next_ + 1) % kChunks;
        buf.assign(pcm, pcm + frames * 2);
        XAUDIO2_BUFFER xb{};
        xb.AudioBytes = UINT32(frames * 4);
        xb.pAudioData = reinterpret_cast<const BYTE*>(buf.data());
        return SUCCEEDED(source_->SubmitSourceBuffer(&xb));
    }

    void start() override { source_->Start(0); }

    uint64_t played_frames() override {
        XAUDIO2_VOICE_STATE st{};
        source_->GetState(&st, 0);
        return st.SamplesPlayed;
    }

private:
    IXAudio2* xa_ = nullptr;
    IXAudio2MasteringVoice* master_ = nullptr;
    IXAudio2SourceVoice* source_ = nullptr;
    bool com_ = false;
    std::vector<int16_t> ring_[kChunks];
    int ring_next_ = 0;
};

}  // namespace

std::unique_ptr<AudioDevice> open_audio_device() {
    auto d = std::make_unique<XAudio2Device>();
    if (!d->open()) return nullptr;
    return d;
}

}  // namespace nf
