// Os adaptadores de vídeo no macOS: o backend de render/gpu_adapters.h com o Metal (MTLCopyAllDevices; os detalhes em
// gpu_adapters_metal.h). Compilado com ARC (-fobjc-arc).
//
// As ordens de preferência (power_rank, perf_rank; platform/gpu_adapter.h): o DXGI as dá prontas; aqui saem das
// propriedades de cada dispositivo. Menor consumo: primeiro os de baixo consumo (isLowPower: os gráficos integrados da
// Intel num Mac com duas placas), depois os de memória unificada, depois o resto, cada grupo na ordem da lista. Alto
// desempenho: o contrário (as placas dedicadas primeiro). Num Mac com Apple Silicon há um dispositivo só e as duas ordens
// são ele. `classify` não muda nada aqui: a memória unificada vem de graça (hasUnifiedMemory), sem um dispositivo de
// teste como no Windows.

#include "render/gpu_adapters.h"
#include "render/gpu_adapters_metal.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace nf {

namespace {

bool has_word(const std::string& name, const char* word) {
    std::string a = name, b = word;
    for (char& c : a) c = char(std::tolower(uint8_t(c)));
    for (char& c : b) c = char(std::tolower(uint8_t(c)));
    return a.find(b) != std::string::npos;
}

uint32_t vendor_from_name(const std::string& name) {
    if (has_word(name, "apple")) return 0x106b;
    if (has_word(name, "amd") || has_word(name, "radeon")) return 0x1002;
    if (has_word(name, "intel")) return 0x8086;
    if (has_word(name, "nvidia") || has_word(name, "geforce") || has_word(name, "quadro")) return 0x10de;
    return 0;
}

}  // namespace

AdapterInfo metal_adapter_info(id<MTLDevice> device, int index) {
    AdapterInfo a;
    a.index = index;
    if (device == nil) return a;
    const char* name = device.name.UTF8String;
    a.name = name != nullptr ? name : "";
    a.vendor_id = vendor_from_name(a.name);
    a.integrated = device.hasUnifiedMemory;
    a.uma_known = true;
    const uint64_t memory = device.recommendedMaxWorkingSetSize;
    a.dedicated_vram = a.integrated ? 0 : memory;
    a.shared_memory = a.integrated ? memory : 0;
    a.outputs = !device.isHeadless;
    const uint64_t id = device.registryID;
    a.luid_low = uint32_t(id & 0xffffffffu);
    a.luid_high = int32_t(uint32_t(id >> 32));
    return a;
}

std::vector<AdapterInfo> enumerate_adapters(bool classify) {
    (void)classify;
    std::vector<AdapterInfo> out;
    @autoreleasepool {
        NSArray<id<MTLDevice>>* all = MTLCopyAllDevices();
        std::vector<int> power_group, perf_group;
        for (NSUInteger i = 0; i < all.count; ++i) {
            id<MTLDevice> d = all[i];
            out.push_back(metal_adapter_info(d, int(i)));
            const int group = d.isLowPower ? 0 : (d.hasUnifiedMemory ? 1 : 2);
            power_group.push_back(group);
            perf_group.push_back(2 - group);
        }
        const auto rank = [&](const std::vector<int>& group, bool power) {
            std::vector<int> order(out.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
            std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return group[size_t(x)] < group[size_t(y)]; });
            for (size_t r = 0; r < order.size(); ++r) {
                AdapterInfo& a = out[size_t(order[r])];
                (power ? a.power_rank : a.perf_rank) = int(r);
            }
        };
        rank(power_group, true);
        rank(perf_group, false);
    }
    return out;
}

}  // namespace nf
