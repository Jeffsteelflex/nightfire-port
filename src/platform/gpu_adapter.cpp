// A escolha do adaptador de vídeo (só do port). Ver gpu_adapter.h.

#include "platform/gpu_adapter.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace nf {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(uint8_t(s[a]))) ++a;
    while (b > a && std::isspace(uint8_t(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

// O primeiro de `list` que passa no filtro, pela ordem de `rank` (o menor primeiro; sem ordem do DXGI, -1, vale a
// ordem da lista).
template <typename Filter>
int first_by_rank(const std::vector<AdapterInfo>& list, int AdapterInfo::*rank, Filter keep) {
    int best = -1;
    for (size_t i = 0; i < list.size(); ++i) {
        const AdapterInfo& a = list[i];
        if (a.software || !keep(a)) continue;
        if (best < 0) {
            best = int(i);
            continue;
        }
        const int r = a.*rank, rb = list[size_t(best)].*rank;
        if (r >= 0 && (rb < 0 || r < rb)) best = int(i);
    }
    return best;
}

}  // namespace

bool parse_adapter_preference(const std::string& text, AdapterPreference* out) {
    const std::string t = trim(text);
    if (t.empty()) return false;
    const std::string l = lower(t);
    AdapterPreference p;
    if (l == "auto" || l == "default") {
        p.kind = AdapterPrefKind::Auto;
    } else if (l == "integrated" || l == "igpu") {
        p.kind = AdapterPrefKind::Integrated;
    } else if (l == "discrete" || l == "dedicated" || l == "dgpu") {
        p.kind = AdapterPrefKind::Discrete;
    } else if (t.size() <= 3 && t.find_first_not_of("0123456789") == std::string::npos) {
        p.kind = AdapterPrefKind::Index;
        p.index = std::stoi(t);
    } else {
        p.kind = AdapterPrefKind::Name;
        p.name = t;
    }
    *out = p;
    return true;
}

std::string adapter_preference_name(const AdapterPreference& p) {
    switch (p.kind) {
        case AdapterPrefKind::Integrated: return "integrated";
        case AdapterPrefKind::Discrete: return "discrete";
        case AdapterPrefKind::Index: return std::to_string(std::max(0, p.index));
        case AdapterPrefKind::Name: return p.name.empty() ? std::string("auto") : p.name;
        default: return "auto";
    }
}

int choose_adapter(const std::vector<AdapterInfo>& list, const AdapterPreference& pref) {
    switch (pref.kind) {
        case AdapterPrefKind::Integrated:
            return first_by_rank(list, &AdapterInfo::power_rank, [](const AdapterInfo& a) { return a.integrated; });
        case AdapterPrefKind::Discrete:
            return first_by_rank(list, &AdapterInfo::perf_rank, [](const AdapterInfo& a) { return !a.integrated; });
        case AdapterPrefKind::Index:
            for (size_t i = 0; i < list.size(); ++i) {
                if (list[i].index == pref.index && !list[i].software) return int(i);
            }
            return -1;
        case AdapterPrefKind::Name: {
            const std::string want = lower(trim(pref.name));
            if (want.empty()) return -1;
            for (size_t i = 0; i < list.size(); ++i) {
                if (!list[i].software && lower(list[i].name).find(want) != std::string::npos) return int(i);
            }
            return -1;
        }
        default:
            return -1;
    }
}

const char* adapter_vendor_name(uint32_t vendor_id) {
    switch (vendor_id) {
        case 0x10de: return "NVIDIA";
        case 0x1002: case 0x1022: return "AMD";
        case 0x8086: return "Intel";
        case 0x1414: return "Microsoft";
        case 0x5143: return "Qualcomm";
        case 0x13b5: return "ARM";
        default: return "?";
    }
}

const char* adapter_kind_name(const AdapterInfo& a) {
    if (a.software) return "software";
    return a.integrated ? "integrated" : "discrete";
}

std::string describe_adapter(const AdapterInfo& a) {
    char buf[384];
    std::snprintf(buf, sizeof(buf), "%d: %s | %s %04x:%04x | %s%s | VRAM dedicada %llu MB, compartilhada %llu MB | %s",
                  a.index, a.name.c_str(), adapter_vendor_name(a.vendor_id), a.vendor_id, a.device_id,
                  adapter_kind_name(a), a.uma_known || a.software ? "" : " (pela VRAM)",
                  static_cast<unsigned long long>(a.dedicated_vram >> 20),
                  static_cast<unsigned long long>(a.shared_memory >> 20), a.outputs ? "com monitor" : "sem monitor");
    return buf;
}

}  // namespace nf
