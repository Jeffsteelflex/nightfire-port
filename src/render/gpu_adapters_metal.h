#pragma once
// Os adaptadores de vídeo no macOS (só do port; render/gpu_adapters.h, docs/port.md "macOS: o renderizador Metal"): o
// AdapterInfo de um MTLDevice. Um cabeçalho do backend do Metal (só os .mm dele o incluem).
//
// Os campos vêm do MTLDevice: o índice é a posição em MTLCopyAllDevices (a ordem do --list-adapters e de [video] adapter
// = <n>), o nome é o do dispositivo, integrado = a memória unificada (hasUnifiedMemory: os Macs com Apple Silicon e os
// gráficos da Intel), a memória é o recommendedMaxWorkingSetSize (dedicada nas placas sem memória unificada,
// compartilhada nas outras), "com monitor" = não headless, e o registryID vai nos campos do LUID (luid_low, luid_high),
// que o renderizador usa para reabrir o mesmo dispositivo. O fabricante (vendor_id) sai do nome (o Metal não dá o PCI):
// Apple 0x106b, AMD 0x1002, Intel 0x8086, NVIDIA 0x10de; device_id fica 0.

#import <Metal/Metal.h>

#include "platform/gpu_adapter.h"

namespace nf {

AdapterInfo metal_adapter_info(id<MTLDevice> device, int index);

}  // namespace nf
