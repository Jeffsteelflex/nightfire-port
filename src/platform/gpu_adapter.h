#pragma once
// A escolha do adaptador de vídeo (só do port: o PS2 tem um GS só). A parte sem GPU: a preferência do
// nightfire.ini e da linha de comando ([video] adapter, --adapter), a lista de adaptadores como o DXGI os
// descreve (render/gpu_adapters.h enumera) e a regra que escolhe um deles. Testável sem placa de vídeo.
//
// Preferência: auto (o padrão: o adaptador padrão do sistema, D3D11CreateDevice sem adaptador, que segue a
// escolha "Preferência de gráficos" do Windows para o executável; o comportamento de antes), integrated (o
// integrado de menor consumo), discrete (o dedicado de maior desempenho), um número (o índice da lista do
// --list-adapters, a ordem do DXGI) ou um trecho do nome (sem diferenciar maiúsculas: "radeon", "rtx 5090").
//
// Escolha (choose_adapter): integrated pega, entre os integrados, o primeiro da ordem de menor consumo do DXGI
// (EnumAdapterByGpuPreference MINIMUM_POWER); discrete, entre os dedicados, o primeiro da ordem de alto
// desempenho; o índice, o da lista; o nome, o primeiro (na ordem da lista) que o contém. O adaptador de
// software (Microsoft Basic Render Driver) nunca é escolhido. Sem nenhum que sirva, -1: o padrão do sistema,
// como auto (o relatório diz que a preferência não achou adaptador).
//
// Integrado ou dedicado: a memória unificada que o próprio Direct3D informa (D3D11_FEATURE_D3D11_OPTIONS2,
// UnifiedMemoryArchitecture, num dispositivo criado só para perguntar); sem ela, pela memória dedicada (menos
// de 1 GiB = integrado).

#include <cstdint>
#include <string>
#include <vector>

namespace nf {

enum class AdapterPrefKind : uint8_t { Auto = 0, Integrated = 1, Discrete = 2, Index = 3, Name = 4 };

struct AdapterPreference {
    AdapterPrefKind kind = AdapterPrefKind::Auto;
    int index = -1;    // Index
    std::string name;  // Name: o trecho procurado, como escrito
    bool operator==(const AdapterPreference&) const = default;
};

// "auto", "integrated", "discrete", um número (>= 0) ou um trecho do nome (o resto). Vazio (ou só espaços): falso.
bool parse_adapter_preference(const std::string& text, AdapterPreference* out);
// O texto do ini (a volta de parse_adapter_preference).
std::string adapter_preference_name(const AdapterPreference& p);

struct AdapterInfo {
    int index = 0;             // a posição na enumeração do DXGI (EnumAdapters1 / ordem sem preferência)
    std::string name;          // DXGI_ADAPTER_DESC1::Description, em UTF-8
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    uint64_t dedicated_vram = 0;   // bytes
    uint64_t shared_memory = 0;    // bytes
    bool integrated = false;       // memória unificada (UMA)
    bool uma_known = false;        // integrated veio do Direct3D (falso: da memória dedicada)
    bool software = false;         // DXGI_ADAPTER_FLAG_SOFTWARE
    int power_rank = -1;           // posição na ordem de menor consumo (MINIMUM_POWER); -1 sem IDXGIFactory6
    int perf_rank = -1;            // posição na ordem de alto desempenho (HIGH_PERFORMANCE)
    uint32_t luid_low = 0;
    int32_t luid_high = 0;
    bool outputs = false;          // tem monitor ligado (EnumOutputs)
};

// A posição em `list` do adaptador que a preferência pede, ou -1 (o padrão do sistema).
int choose_adapter(const std::vector<AdapterInfo>& list, const AdapterPreference& pref);
// O fabricante pelo PCI vendor id (NVIDIA, AMD, Intel, Microsoft...); "?" se desconhecido.
const char* adapter_vendor_name(uint32_t vendor_id);
// Uma linha: o índice, o nome, o fabricante, a memória dedicada, integrado ou dedicado e se tem monitor.
std::string describe_adapter(const AdapterInfo& a);
// O tipo em uma palavra para os menus e o relatório: "integrated" ou "discrete" (ou "software").
const char* adapter_kind_name(const AdapterInfo& a);

}  // namespace nf
