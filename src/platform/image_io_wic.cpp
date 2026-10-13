// Imagens em arquivo pelo WIC (Windows Imaging Component): o backend do Windows de platform/image_io.h. As chamadas são
// as que o renderizador, a camada HD e o nf_driving faziam antes desta interface.

#include "platform/image_io.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace nf {

namespace {

template <typename T>
using Com = Microsoft::WRL::ComPtr<T>;

void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s falhou (HRESULT 0x%08lX)", what, static_cast<unsigned long>(hr));
        throw std::runtime_error(buf);
    }
}

}  // namespace

bool write_png_rgba(const std::wstring& path, int width, int height, const uint8_t* rgba) {
    if (width <= 0 || height <= 0) return false;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Com<IWICImagingFactory> wic;
    Com<IWICStream> stream;
    Com<IWICBitmapEncoder> enc;
    Com<IWICBitmapFrameEncode> fr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
        FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) || FAILED(enc->CreateNewFrame(&fr, nullptr)) ||
        FAILED(fr->Initialize(nullptr)) || FAILED(fr->SetSize(UINT(width), UINT(height)))) {
        return false;
    }
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
    if (FAILED(fr->SetPixelFormat(&fmt))) return false;
    std::vector<uint8_t> pixels(rgba, rgba + size_t(width) * size_t(height) * 4);
    if (fmt != GUID_WICPixelFormat32bppRGBA) {  // o codificador pediu BGRA
        for (size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
    }
    return SUCCEEDED(fr->WritePixels(UINT(height), UINT(width) * 4, UINT(pixels.size()), pixels.data())) &&
           SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
}

void write_png_bgra(const std::wstring& path, int width, int height, const uint8_t* bgra) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Com<IWICImagingFactory> wic;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)), "WIC");
    Com<IWICStream> stream;
    check(wic->CreateStream(&stream), "WIC stream");
    check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "arquivo PNG");
    Com<IWICBitmapEncoder> enc;
    check(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc), "encoder PNG");
    check(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache), "encoder");
    Com<IWICBitmapFrameEncode> fr;
    check(enc->CreateNewFrame(&fr, nullptr), "frame");
    check(fr->Initialize(nullptr), "frame");
    check(fr->SetSize(UINT(width), UINT(height)), "tamanho");
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    check(fr->SetPixelFormat(&fmt), "formato");
    // O WritePixels só lê os pixels (o BYTE* sem const é da assinatura do WIC): sem cópia.
    const UINT size = UINT(width) * UINT(height) * 4;
    check(fr->WritePixels(UINT(height), UINT(width) * 4, size, const_cast<BYTE*>(bgra)), "WritePixels");
    check(fr->Commit(), "Commit");
    check(enc->Commit(), "Commit");
}

bool read_image_bgra(const std::wstring& path, int max_side, int* width, int* height, std::vector<uint8_t>* bgra) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Com<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    Com<IWICBitmapDecoder> dec;
    Com<IWICBitmapFrameDecode> frame_dec;
    Com<IWICFormatConverter> conv;
    UINT w = 0, h = 0;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) ||
        FAILED(dec->GetFrame(0, &frame_dec)) || FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame_dec.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom)) ||
        FAILED(conv->GetSize(&w, &h)) || w == 0 || h == 0 || int(w) > max_side || int(h) > max_side) {
        return false;
    }
    bgra->assign(size_t(w) * h * 4, 0);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, UINT(bgra->size()), bgra->data()))) return false;
    *width = int(w);
    *height = int(h);
    return true;
}

}  // namespace nf
