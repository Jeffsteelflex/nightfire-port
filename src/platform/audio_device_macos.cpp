// O aparelho de som no macOS: o backend de platform/audio_device.h com o Core Audio (a AudioUnit de saída padrão,
// kAudioUnitSubType_DefaultOutput; docs/port.md, "macOS: os backends"). Ver audio_device.h e audio_out.h.
//
// As mesmas regras do backend do XAudio2 (audio_device_xaudio2.cpp): uma fila de até 16 pedaços de PCM de 48 kHz,
// estéreo, 16 bits, que o audio_out.cpp enche (o pré-enchimento de 50 ms, no máximo 8 na fila, o resto descartado);
// queued_buffers() conta os pedaços ainda não tocados até o fim e played_frames() as amostras tocadas desde a criação (o
// relógio dos filmes: audio_out_queued_frames = entregues - tocadas). Como o SamplesPlayed do XAudio2, só contam as
// amostras dos pedaços: com a fila vazia a saída toca silêncio e o relógio para.
//
// As threads: o submit vem da thread do jogo e o Core Audio pede as amostras na thread de tempo real dele (o render
// callback). A fila é um anel de pedaços com um produtor e um consumidor (as posições em atômicos, sem trava na thread
// de tempo real); o pedaço é copiado na thread do jogo. A AudioUnit padrão converte 48 kHz para a taxa do aparelho e
// segue o aparelho padrão quando ele muda (fones ligados, AirPods).
//
// Sem aparelho (uma máquina virtual sem som, a CI): a criação ou o início falham e open_audio_device devolve nulo; o jogo
// segue mudo, como no Windows sem placa de som.

#include "platform/audio_device.h"

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

#include <atomic>
#include <cstring>
#include <vector>

namespace nf {

namespace {

constexpr int kChunks = 16;  // pedaços no anel (audio_out.cpp não deixa passar de 8 na fila)

class CoreAudioDevice final : public AudioDevice {
public:
    ~CoreAudioDevice() override {
        if (unit_ != nullptr) {
            if (started_) AudioOutputUnitStop(unit_);
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
            unit_ = nullptr;
        }
    }

    bool open() {
        // Sem um aparelho de saída padrão (uma máquina virtual sem som): mudo, sem nem criar a AudioUnit. O elemento 0 é
        // o kAudioObjectPropertyElementMain (macOS 12; o nome antigo, Master, está obsoleto).
        AudioObjectPropertyAddress addr{kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, 0};
        AudioDeviceID device = kAudioObjectUnknown;
        UInt32 size = sizeof(device);
        if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &device) != noErr ||
            device == kAudioObjectUnknown) {
            return false;
        }
        AudioComponentDescription desc{};
        desc.componentType = kAudioUnitType_Output;
        desc.componentSubType = kAudioUnitSubType_DefaultOutput;
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
        if (comp == nullptr || AudioComponentInstanceNew(comp, &unit_) != noErr || unit_ == nullptr) {
            unit_ = nullptr;
            return false;
        }
        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = 48000.0;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        fmt.mFramesPerPacket = 1;
        fmt.mChannelsPerFrame = 2;
        fmt.mBitsPerChannel = 16;
        fmt.mBytesPerFrame = 4;
        fmt.mBytesPerPacket = 4;
        if (AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt)) !=
            noErr) {
            return false;
        }
        AURenderCallbackStruct cb{};
        cb.inputProc = &CoreAudioDevice::render;
        cb.inputProcRefCon = this;
        if (AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb)) !=
            noErr) {
            return false;
        }
        return AudioUnitInitialize(unit_) == noErr;
    }

    int queued_buffers() override {
        const uint64_t head = head_.load(std::memory_order_acquire);
        const uint64_t tail = tail_.load(std::memory_order_acquire);
        return int(head - tail);
    }

    bool submit(const int16_t* pcm, int frames) override {
        if (frames <= 0) return true;
        const uint64_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= uint64_t(kChunks)) return false;  // o anel cheio
        Chunk& c = ring_[head % kChunks];
        c.pcm.assign(pcm, pcm + size_t(frames) * 2);
        c.frames = frames;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    void start() override {
        if (unit_ != nullptr && !started_) started_ = AudioOutputUnitStart(unit_) == noErr;
    }

    uint64_t played_frames() override { return played_.load(std::memory_order_acquire); }

    bool started() const { return started_; }

private:
    struct Chunk {
        std::vector<int16_t> pcm;
        int frames = 0;
    };

    // A thread de tempo real do Core Audio: tira amostras do anel (silêncio no que faltar).
    static OSStatus render(void* ref, AudioUnitRenderActionFlags* flags, const AudioTimeStamp*, UInt32, UInt32 frames,
                           AudioBufferList* data) {
        auto* self = static_cast<CoreAudioDevice*>(ref);
        if (data == nullptr || data->mNumberBuffers < 1 || data->mBuffers[0].mData == nullptr) return noErr;
        auto* out = static_cast<int16_t*>(data->mBuffers[0].mData);
        const UInt32 capacity = data->mBuffers[0].mDataByteSize / 4;
        UInt32 want = frames < capacity ? frames : capacity;
        UInt32 done = 0;
        uint64_t tail = self->tail_.load(std::memory_order_relaxed);
        while (done < want) {
            if (tail == self->head_.load(std::memory_order_acquire)) break;
            const Chunk& c = self->ring_[tail % kChunks];
            const int left = c.frames - self->offset_;
            const UInt32 take = UInt32(left) < want - done ? UInt32(left) : want - done;
            std::memcpy(out + size_t(done) * 2, c.pcm.data() + size_t(self->offset_) * 2, size_t(take) * 4);
            done += take;
            self->offset_ += int(take);
            if (self->offset_ >= c.frames) {
                self->offset_ = 0;
                ++tail;
                self->tail_.store(tail, std::memory_order_release);
            }
        }
        if (done < want) std::memset(out + size_t(done) * 2, 0, size_t(want - done) * 4);
        if (done == 0 && flags != nullptr) *flags |= kAudioUnitRenderAction_OutputIsSilence;
        self->played_.fetch_add(done, std::memory_order_acq_rel);
        return noErr;
    }

    AudioComponentInstance unit_ = nullptr;
    bool started_ = false;
    Chunk ring_[kChunks];
    std::atomic<uint64_t> head_{0};  // o próximo pedaço a escrever (a thread do jogo)
    std::atomic<uint64_t> tail_{0};  // o pedaço tocando (a thread do Core Audio)
    int offset_ = 0;                 // os frames já tocados do pedaço da cauda (só a thread do Core Audio)
    std::atomic<uint64_t> played_{0};
};

}  // namespace

std::unique_ptr<AudioDevice> open_audio_device() {
    auto d = std::make_unique<CoreAudioDevice>();
    if (!d->open()) return nullptr;
    return d;
}

}  // namespace nf
