// Os adaptadores de vídeo pelo DXGI (só do port; o backend do Windows). Ver gpu_adapters.h e gpu_adapters_dxgi.h.

#include "render/gpu_adapters_dxgi.h"

#include <d3d11_3.h>
#include <dxgi1_6.h>

#include <string>

namespace nf {

namespace {

template <typename T>
using Com = Microsoft::WRL::ComPtr<T>;

std::string utf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string s(size_t(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

bool same_luid(const LUID& a, const AdapterInfo& b) { return a.LowPart == b.luid_low && a.HighPart == b.luid_high; }

// A memória unificada pelo próprio Direct3D: um dispositivo de teste no adaptador.
bool query_uma(IDXGIAdapter1* adapter, bool* uma) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    Com<ID3D11Device> dev;
    if (FAILED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &dev,
                                 nullptr, nullptr))) {
        return false;
    }
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 o2{};
    if (FAILED(dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &o2, sizeof(o2)))) return false;
    *uma = o2.UnifiedMemoryArchitecture != FALSE;
    return true;
}

}  // namespace

AdapterInfo adapter_info(IDXGIAdapter1* adapter, int index) {
    AdapterInfo a;
    a.index = index;
    DXGI_ADAPTER_DESC1 d{};
    if (adapter == nullptr || FAILED(adapter->GetDesc1(&d))) return a;
    a.name = utf8(d.Description);
    a.vendor_id = d.VendorId;
    a.device_id = d.DeviceId;
    a.dedicated_vram = d.DedicatedVideoMemory;
    a.shared_memory = d.SharedSystemMemory;
    a.software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    a.luid_low = d.AdapterLuid.LowPart;
    a.luid_high = d.AdapterLuid.HighPart;
    a.integrated = !a.software && a.dedicated_vram < (uint64_t(1) << 30);
    Com<IDXGIOutput> out;
    a.outputs = SUCCEEDED(adapter->EnumOutputs(0, &out));
    return a;
}

std::vector<AdapterInfo> enumerate_adapters(bool classify) {
    std::vector<AdapterInfo> list;
    Com<IDXGIFactory1> f1;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f1)))) return list;
    Com<IDXGIFactory6> f6;
    f1.As(&f6);
    std::vector<Com<IDXGIAdapter1>> adapters;
    for (UINT i = 0;; ++i) {
        Com<IDXGIAdapter1> ad;
        const HRESULT hr = f6 != nullptr ? f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_UNSPECIFIED, IID_PPV_ARGS(&ad))
                                         : f1->EnumAdapters1(i, &ad);
        if (FAILED(hr)) break;
        list.push_back(adapter_info(ad.Get(), int(i)));
        adapters.push_back(ad);
    }
    // As ordens do Windows: de menor consumo (o integrado primeiro num sistema híbrido) e de alto desempenho.
    if (f6 != nullptr) {
        for (const auto& [pref, rank] : {std::pair{DXGI_GPU_PREFERENCE_MINIMUM_POWER, &AdapterInfo::power_rank},
                                         std::pair{DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, &AdapterInfo::perf_rank}}) {
            for (UINT i = 0;; ++i) {
                Com<IDXGIAdapter1> ad;
                if (FAILED(f6->EnumAdapterByGpuPreference(i, pref, IID_PPV_ARGS(&ad)))) break;
                DXGI_ADAPTER_DESC1 d{};
                if (FAILED(ad->GetDesc1(&d))) continue;
                for (AdapterInfo& a : list) {
                    if (same_luid(d.AdapterLuid, a)) a.*rank = int(i);
                }
            }
        }
    }
    if (classify) {
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].software) continue;
            bool uma = false;
            if (query_uma(adapters[i].Get(), &uma)) {
                list[i].integrated = uma;
                list[i].uma_known = true;
            }
        }
    }
    return list;
}

Com<IDXGIAdapter1> open_adapter(const AdapterInfo& a) {
    Com<IDXGIFactory1> f1;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f1)))) return nullptr;
    for (UINT i = 0;; ++i) {
        Com<IDXGIAdapter1> ad;
        if (FAILED(f1->EnumAdapters1(i, &ad))) break;
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(ad->GetDesc1(&d)) && same_luid(d.AdapterLuid, a)) return ad;
    }
    return nullptr;
}

}  // namespace nf
