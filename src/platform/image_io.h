#pragma once
// Imagens em arquivo (só do port; docs/port.md, "Plataforma: interfaces e backends"): as capturas em PNG, os recortes do
// inventário do 2D, as substitutas PNG da camada HD. Este cabeçalho não traz nada do sistema; o backend do Windows é o
// WIC (image_io_wic.cpp, windowscodecs); no macOS será o ImageIO (etapa 3).

#include <cstdint>
#include <string>
#include <vector>

namespace nf {

// Um PNG RGBA de 8 bits por canal (false se não gravou).
bool write_png_rgba(const std::wstring& path, int width, int height, const uint8_t* rgba);
// Um PNG BGRA de width * height * 4 bytes (a captura do quadro: Renderer::save_png). Lança std::runtime_error com o passo
// que falhou.
void write_png_bgra(const std::wstring& path, int width, int height, const uint8_t* bgra);
// Lê uma imagem (PNG e o que mais o sistema souber) em BGRA de 8 bits, sem o alfa pré-multiplicado; false se não leu
// ou se um lado passa de max_side.
bool read_image_bgra(const std::wstring& path, int max_side, int* width, int* height, std::vector<uint8_t>* bgra);

}  // namespace nf
