#pragma once
// Os adaptadores de vídeo pelo DXGI (o backend do Windows de render/gpu_adapters.h; só os arquivos do backend o incluem).
//
// enumerate_adapters: IDXGIFactory6::EnumAdapterByGpuPreference (a ordem sem preferência dá o índice; as ordens de
// menor consumo e de alto desempenho dão power_rank e perf_rank), ou IDXGIFactory1::EnumAdapters1 sem a fábrica 6.
// Com `classify`, cada adaptador ganha um dispositivo Direct3D 11 de teste (nível 11_0, sem janela) só para
// perguntar se a memória é unificada (D3D11_FEATURE_D3D11_OPTIONS2).
//
// open_adapter: o IDXGIAdapter1 de um item da lista (pelo LUID), para o Renderer criar o dispositivo nele
// (D3D_DRIVER_TYPE_UNKNOWN). O Direct2D e o DirectWrite das camadas 2D e dos menus Modern nascem do mesmo
// dispositivo (IDXGIDevice), então ficam no mesmo adaptador.

#include <dxgi1_2.h>
#include <wrl/client.h>

#include "render/gpu_adapters.h"

namespace nf {

Microsoft::WRL::ComPtr<IDXGIAdapter1> open_adapter(const AdapterInfo& a);
// Os dados de um adaptador (sem a classificação; integrated pela VRAM).
AdapterInfo adapter_info(IDXGIAdapter1* adapter, int index);

}  // namespace nf
