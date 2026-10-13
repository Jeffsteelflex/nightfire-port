#pragma once
// Os modos do monitor de uma janela, pelo sistema (só do port; docs/port.md, "macOS: os backends"). Só fora do Windows:
// lá o renderizador do Direct3D 11 tira os modos do DXGI (render/renderer_d3d11.cpp). No macOS os renderizadores (o nulo
// da etapa 3c, render/renderer_null.cpp; o Metal da 3d) os pedem aqui, e o backend da janela responde
// (window_macos.mm: CGDisplayCopyAllDisplayModes no monitor da janela, os tamanhos em pixels, com os modos HiDPI).

#include <vector>

#include "platform/video_settings.h"

namespace nf {

// native_window: Window::native_handle() (nulo: o monitor principal). Os tamanhos em pixels, sem repetir (a ordem não
// importa: nf::resolution_choices ordena), e o modo em uso do monitor (a área de trabalho em pixels). false sem monitor
// (as saídas ficam como estavam).
bool system_display_modes(void* native_window, std::vector<DisplayMode>* modes, DisplayMode* desktop);

}  // namespace nf
