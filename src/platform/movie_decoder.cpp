// O decodificador de MPEG-2 dos filmes pelo Media Foundation: ver movie_decoder.h.

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

// Os coeficientes do CSC do IPU, em 1/128 (Y 1.164, Cr->R 1.594, Cb->G -0.391, Cr->G -0.8125, Cb->B 2.016): BT.601 de
// faixa limitada.
constexpr int kY = 149, kRCr = 204, kGCb = -50, kGCr = -104, kBCb = 258;

uint8_t clamp8(int v) { return uint8_t(std::clamp(v, 0, 255)); }

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

}  // namespace

void yuv_to_bgr(int y, int cb, int cr, uint8_t* bgr) {
    const int yy = kY * (y - 16);
    const int u = cb - 128, v = cr - 128;
    bgr[0] = clamp8((yy + kBCb * u + 64) >> 7);
    bgr[1] = clamp8((yy + kGCb * u + kGCr * v + 64) >> 7);
    bgr[2] = clamp8((yy + kRCr * v + 64) >> 7);
}

void nv12_to_bgra(const uint8_t* nv12, int width, int height, uint8_t* bgra, uint8_t alpha) {
    const uint8_t* uv_plane = nv12 + size_t(width) * size_t(height);
    for (int y = 0; y < height; ++y) {
        const uint8_t* yr = nv12 + size_t(y) * size_t(width);
        const uint8_t* uvr = uv_plane + size_t(y / 2) * size_t(width);
        uint8_t* out = bgra + size_t(y) * size_t(width) * 4;
        for (int x = 0; x < width; x += 2) {
            // O croma de cada bloco de 2 x 2 vale para os quatro pixels (como o CSC do IPU, sem interpolar).
            const int u = int(uvr[x]) - 128, v = int(uvr[x + 1]) - 128;
            const int b = kBCb * u + 64, g = kGCb * u + kGCr * v + 64, r = kRCr * v + 64;
            for (int k = 0; k < 2 && x + k < width; ++k) {
                const int yy = kY * (int(yr[x + k]) - 16);
                out[0] = clamp8((yy + b) >> 7);
                out[1] = clamp8((yy + g) >> 7);
                out[2] = clamp8((yy + r) >> 7);
                out[3] = alpha;
                out += 4;
            }
        }
    }
}

Mpeg2Decoder::~Mpeg2Decoder() { close(); }

void Mpeg2Decoder::close() {
    if (mft_ != nullptr) {
        mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        mft_->Release();
        mft_ = nullptr;
    }
    if (mf_started_) MFShutdown();
    mf_started_ = false;
    if (com_) CoUninitialize();
    com_ = false;
    frames_.clear();
    spare_.clear();
}

bool Mpeg2Decoder::set_output_nv12() {
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
            UINT32 stride = 0;
            stride_ = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? int(int32_t(stride)) : width_;
            if (stride_ < width_) stride_ = width_;
            type->Release();
            MFT_OUTPUT_STREAM_INFO osi{};
            mft_->GetOutputStreamInfo(0, &osi);
            out_size_ = osi.cbSize;
            provides_samples_ = (osi.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
            return true;
        }
        type->Release();
    }
}

bool Mpeg2Decoder::open(int width, int height, double fps) {
    close();
    failure_.clear();
    name_.clear();
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    com_ = SUCCEEDED(co);
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
        failure_ = "MFStartup";
        close();
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
        failure_ = "nenhum MFT de MPEG-2";
        if (acts != nullptr) CoTaskMemFree(acts);
        close();
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
            std::snprintf(buf, sizeof(buf), "%s%s ativacao %08lx", failure_.empty() ? "" : "; ", nm.c_str(),
                          static_cast<unsigned long>(a));
            failure_ += buf;
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
            std::snprintf(buf, sizeof(buf), "%s%s entrada %08lx", failure_.empty() ? "" : "; ", nm.c_str(),
                          static_cast<unsigned long>(a));
            failure_ += buf;
            t->Release();
            continue;
        }
        mft_ = t;
        width_ = width;
        height_ = height;
        if (!set_output_nv12()) {
            failure_ += (failure_.empty() ? "" : "; ") + nm + " sem NV12";
            mft_->Release();
            mft_ = nullptr;
            continue;
        }
        name_ = nm;
    }
    for (UINT32 i = 0; i < count; ++i) acts[i]->Release();
    CoTaskMemFree(acts);
    if (mft_ == nullptr) {
        close();
        return false;
    }
    duration_ = fps > 0.0 ? int64_t(10000000.0 / fps + 0.5) : 333333;
    time_ = 0;
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    return true;
}

bool Mpeg2Decoder::feed(const uint8_t* data, size_t size) {
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
            pull();
            continue;
        }
        ok = SUCCEEDED(hr);
        break;
    }
    sample->Release();
    pull();
    return ok;
}

void Mpeg2Decoder::drain() {
    if (mft_ == nullptr) return;
    mft_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    pull();
}

void Mpeg2Decoder::flush() {
    if (mft_ == nullptr) return;
    mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    for (MovieFrameNV12& f : frames_) spare_.push_back(std::move(f));
    frames_.clear();
    time_ = 0;
}

void Mpeg2Decoder::pull() {
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
                MovieFrameNV12 f;
                if (!spare_.empty()) {
                    f = std::move(spare_.back());
                    spare_.pop_back();
                }
                f.width = width_;
                f.height = height_;
                const size_t need = size_t(width_) * size_t(height_) * 3 / 2;
                f.data.resize(need);
                // Copia linha a linha pelo passo do decodificador (a saída fica com passo = largura).
                const size_t stride = size_t(stride_);
                const size_t rows_y = size_t(height_), rows_uv = size_t(height_) / 2;
                const size_t plane = stride * rows_y;
                if (len >= plane + stride * rows_uv) {
                    for (size_t y = 0; y < rows_y; ++y) std::memcpy(&f.data[y * size_t(width_)], p + y * stride, size_t(width_));
                    for (size_t y = 0; y < rows_uv; ++y) {
                        std::memcpy(&f.data[(rows_y + y) * size_t(width_)], p + plane + y * stride, size_t(width_));
                    }
                    frames_.push_back(std::move(f));
                    ++frames_out_;
                }
                b->Unlock();
            }
            b->Release();
        }
        if (own == nullptr) ob.pSample->Release();
        release(own);
    }
}

bool Mpeg2Decoder::take(MovieFrameNV12& out) {
    if (frames_.empty()) return false;
    if (!out.data.empty()) spare_.push_back(std::move(out));
    out = std::move(frames_.front());
    frames_.pop_front();
    return true;
}

}  // namespace nf
