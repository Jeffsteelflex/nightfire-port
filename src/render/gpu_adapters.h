#pragma once
// Os adaptadores de vídeo do sistema (só do port; a regra da escolha está em platform/gpu_adapter.h). Este cabeçalho não
// traz nada da API gráfica: o backend do Windows é o DXGI (render/gpu_adapters_dxgi.cpp; os detalhes em
// gpu_adapters_dxgi.h); no macOS serão os MTLDevice (etapa 3).
//
// enumerate_adapters: a lista na ordem do sistema (o índice do --list-adapters e de [video] adapter = <n>), com as ordens
// de menor consumo e de alto desempenho (power_rank e perf_rank). Com `classify`, cada adaptador é perguntado se a
// memória é unificada (integrado): no Windows um dispositivo Direct3D 11 de teste por placa (uns 20 a 100 ms cada), então
// só quando a preferência precisa (integrated, discrete) e no --list-adapters. O Renderer recebe o escolhido (um
// AdapterInfo) e cria o dispositivo nele.

#include <vector>

#include "platform/gpu_adapter.h"

namespace nf {

std::vector<AdapterInfo> enumerate_adapters(bool classify);

}  // namespace nf
