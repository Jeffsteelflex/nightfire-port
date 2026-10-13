// O decodificador de MPEG-2 do sistema pelo Media Foundation (o backend do Windows; mfplat, mfuuid): um MFT na CPU,
// atrás da fachada Mpeg2Decoder (open_system_mpeg2_decoder). Ver movie_decoder.h.

#include "platform/movie_decoder.h"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nf {

namespace {

std::string narrow(const wchar_t* w) {
    if (w == nullptr) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(std::max(n, 1)) - 1, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

template <typename T>
void release(T*& p) {
    if (p != nullptr) p->Release();
    p = nullptr;
}

class MfBackend final : public Mpeg2Decoder::Backend {
public:
    ~MfBackend() override;
    bool open(int width, int height, double fps, std::string* name, std::string* failure);
    bool feed(Mpeg2Decoder& d, const uint8_t* data, size_t size) override;
    void drain(Mpeg2Decoder& d) override;
    void flush() override;

private:
    bool set_output_nv12();
    void pull(Mpeg2Decoder& d);

    IMFTransform* mft_ = nullptr;
    bool mf_started_ = false;
    bool com_ = false;
    int width_ = 0, height_ = 0;
    int stride_ = 0;
    uint32_t out_size_ = 0;
    bool provides_samples_ = false;
    int64_t time_ = 0;
    int64_t duration_ = 0;
};

MfBackend::~MfBackend() {
    if (mft_ != nullptr) {
        mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        mft_->Release();
        mft_ = nullptr;
    }
    if (mf_started_) MFShutdown();
    if (com_) CoUninitialize();
}

bool MfBackend::set_output_nv12() {
    for (DWORD i = 0;; ++i) {
        IMFMediaType* type = nullptr;
        if (FAILED(mft_->GetOutputAvailableType(0, i, &type))) return false;
        GUID sub{};
        type->GetGUID(MF_MT_SUBTYPE, &sub);
        if (sub == MFVideoFormat_NV12 && SUCCEEDED(mft_->SetOutputType(0, type, 0))) {
            UINT32 w = 0, h = 0;
            if (SUCCEEDED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h)) && w != 0 && h != 0) {
                width_ = int(w);
                height_ = int(h);
            }
            UINT32 s = 0;
            stride_ = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &s)) ? int(int32_t(s)) : width_;
            if (stride_ < width_) stride_ = width_;
            type->Release();
            MFT_OUTPUT_STREAM_INFO osi{};
            mft_->GetOutputStreamInfo(0, &osi);
            out_size_ = osi.cbSize;
            provides_samples_ =
                (osi.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
            return true;
        }
        type->Release();
    }
}

bool MfBackend::open(int width, int height, double fps, std::string* name, std::string* failure) {
    std::string& why = *failure;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    com_ = SUCCEEDED(co);
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
        why = "MFStartup";
        return false;
    }
    mf_started_ = true;
    MFT_REGISTER_TYPE_INFO in{MFMediaType_Video, MFVideoFormat_MPEG2};
    IMFActivate** acts = nullptr;
    UINT32 count = 0;
    const HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                                 MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER, &in,
                                 nullptr, &acts, &count);
    if (FAILED(hr) || count == 0) {
        why = "nenhum MFT de MPEG-2";
        if (acts != nullptr) CoTaskMemFree(acts);
        return false;
    }
    for (UINT32 i = 0; i < count && mft_ == nullptr; ++i) {
        WCHAR* wname = nullptr;
        UINT32 len = 0;
        acts[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &wname, &len);
        const std::string nm = narrow(wname);
        CoTaskMemFree(wname);
        IMFTransform* t = nullptr;
        HRESULT a = acts[i]->ActivateObject(IID_PPV_ARGS(&t));
        if (FAILED(a) || t == nullptr) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s%s ativacao %08lx", why.empty() ? "" : "; ", nm.c_str(),
                          static_cast<unsigned long>(a));
            why += buf;
            continue;
        }
        IMFMediaType* it = nullptr;
        MFCreateMediaType(&it);
        it->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        it->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_MPEG2);
        MFSetAttributeSize(it, MF_MT_FRAME_SIZE, UINT32(width), UINT32(height));
        if (fps > 0.0) MFSetAttributeRatio(it, MF_MT_FRAME_RATE, UINT32(fps * 1000.0 + 0.5), 1000);
        a = t->SetInputType(0, it, 0);
        it->Release();
        if (FAILED(a)) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s%s entrada %08lx", why.empty() ? "" : "; ", nm.c_str(),
                          static_cast<unsigned long>(a));
            why += buf;
            t->Release();
            continue;
        }
        mft_ = t;
        width_ = width;
        height_ = height;
        if (!set_output_nv12()) {
            why += (why.empty() ? "" : "; ") + nm + " sem NV12";
            mft_->Release();
            mft_ = nullptr;
            continue;
        }
        *name = nm;
    }
    for (UINT32 i = 0; i < count; ++i) acts[i]->Release();
    CoTaskMemFree(acts);
    if (mft_ == nullptr) return false;
    why.clear();  // um serviu: as tentativas de antes não contam
    duration_ = fps > 0.0 ? int64_t(10000000.0 / fps + 0.5) : 333333;
    time_ = 0;
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    return true;
}

bool MfBackend::feed(Mpeg2Decoder& d, const uint8_t* data, size_t size) {
    if (mft_ == nullptr || size == 0) return false;
    IMFSample* sample = nullptr;
    IMFMediaBuffer* buf = nullptr;
    if (FAILED(MFCreateSample(&sample)) || FAILED(MFCreateMemoryBuffer(DWORD(size), &buf))) {
        release(sample);
        release(buf);
        return false;
    }
    BYTE* p = nullptr;
    buf->Lock(&p, nullptr, nullptr);
    std::memcpy(p, data, size);
    buf->Unlock();
    buf->SetCurrentLength(DWORD(size));
    sample->AddBuffer(buf);
    buf->Release();
    sample->SetSampleTime(time_);
    sample->SetSampleDuration(duration_);
    time_ += duration_;
    bool ok = true;
    for (int tries = 0; tries < 64; ++tries) {
        const HRESULT hr = mft_->ProcessInput(0, sample, 0);
        if (hr == MF_E_NOTACCEPTING) {
            pull(d);
            continue;
        }
        ok = SUCCEEDED(hr);
        break;
    }
    sample->Release();
    pull(d);
    return ok;
}

void MfBackend::drain(Mpeg2Decoder& d) {
    if (mft_ == nullptr) return;
    mft_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    pull(d);
}

void MfBackend::flush() {
    if (mft_ == nullptr) return;
    mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    time_ = 0;
}

void MfBackend::pull(Mpeg2Decoder& d) {
    for (int guard = 0; guard < 64; ++guard) {
        MFT_OUTPUT_DATA_BUFFER ob{};
        IMFSample* own = nullptr;
        if (!provides_samples_) {
            IMFMediaBuffer* b = nullptr;
            if (FAILED(MFCreateSample(&own)) || FAILED(MFCreateMemoryBuffer(std::max<DWORD>(out_size_, 1), &b))) {
                release(own);
                release(b);
                return;
            }
            own->AddBuffer(b);
            b->Release();
            ob.pSample = own;
        }
        DWORD status = 0;
        const HRESULT hr = mft_->ProcessOutput(0, 1, &ob, &status);
        if (ob.pEvents != nullptr) ob.pEvents->Release();
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            release(own);
            return;
        }
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            release(own);
            if (!set_output_nv12()) return;
            continue;
        }
        if (FAILED(hr) || ob.pSample == nullptr) {
            release(own);
            return;
        }
        IMFMediaBuffer* b = nullptr;
        if (SUCCEEDED(ob.pSample->ConvertToContiguousBuffer(&b))) {
            BYTE* p = nullptr;
            DWORD len = 0;
            if (SUCCEEDED(b->Lock(&p, nullptr, &len))) {
                MovieFrameNV12 f = reuse_frame(d);
                f.width = width_;
                f.height = height_;
                const size_t need = size_t(width_) * size_t(height_) * 3 / 2;
                f.data.resize(need);
                // Copia linha a linha pelo passo do decodificador (a saída fica com passo = largura).
                const size_t pitch = size_t(stride_);
                const size_t rows_y = size_t(height_), rows_uv = size_t(height_) / 2;
                const size_t plane = pitch * rows_y;
                if (len >= plane + pitch * rows_uv) {
                    for (size_t y = 0; y < rows_y; ++y) {
                        std::memcpy(&f.data[y * size_t(width_)], p + y * pitch, size_t(width_));
                    }
                    for (size_t y = 0; y < rows_uv; ++y) {
                        std::memcpy(&f.data[(rows_y + y) * size_t(width_)], p + plane + y * pitch, size_t(width_));
                    }
                    push_frame(d, std::move(f));
                }
                b->Unlock();
            }
            b->Release();
        }
        if (own == nullptr) ob.pSample->Release();
        release(own);
    }
}

}  // namespace

std::unique_ptr<Mpeg2Decoder::Backend> open_system_mpeg2_decoder(int width, int height, double fps, std::string* name,
                                                                std::string* failure) {
    auto b = std::make_unique<MfBackend>();
    if (!b->open(width, height, fps, name, failure)) return nullptr;
    return b;
}

}  // namespace nf
