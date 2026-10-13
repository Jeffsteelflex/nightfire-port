#pragma once
// O renderizador Direct3D 11 do nf_driving (docs/driving.md seção 10): desenha um CarModel com os shaders que refazem
// o microprograma da família MatCar (o vertex shader repete os passos 1-7 de driving/mat_car.h; o pixel shader faz o
// que o GS faz: MODULATE/DECAL com o alfa do GS, o teste de alfa com o AFAIL, a névoa, as misturas do ALPHA_1).
//
// O que o PS2 faz e o PC faz diferente (só do port): a janela de vista do GS (as constantes 0 e 1 da VU) e o recorte
// por triângulo do microprograma dão lugar à projeção e ao recorte do Direct3D; o Z cresce para longe (o do GS cresce
// para perto: GEQUAL vira LESS_EQUAL); o alfa sai do shader limitado a 1 (o GS aceita até 2 em As = 0xff; os carros só
// usam até 0x80); a passada que falha o teste de alfa com AFAIL "só RGB" vira um segundo desenho sem Z e sem alfa.
//
// Este cabeçalho não traz nada do sistema: o backend é drv_renderer_d3d11.cpp (Direct3D 11); a janela chega como o
// identificador do sistema (Window::native_handle).

#include <string>

#include "driving/car_model.h"

namespace drv {

struct DrvRenderOptions {
    int width = 1280, height = 720;
    bool spec = true, refl = true, fallback = true;
    float fov_deg = 40.0f;
    int msaa = 4;
    std::string adapter;  // como o [video] adapter do nightfire.exe (vazio: o padrão do sistema)
    int only_part = -1;   // depuração: só os desenhos desta peça
    int view = 1;         // a lista de desenho da vista (CarModel::view_draw_list); 0: todas as instâncias
};

struct DrvRenderStats {
    int draw_calls = 0;
    int pass_calls[3] = {0, 0, 0};
    int fallback_calls = 0;
    int instances = 0;  // peças desenhadas (instâncias com modelo)
    std::string adapter;
};

class DrvRenderer {
public:
    DrvRenderer();
    ~DrvRenderer();
    // window nulo: só o alvo fora da tela (as capturas); senão a janela do sistema (o HWND no Windows).
    bool init(void* window, const DrvRenderOptions& opt, std::string& error);
    bool upload(const CarModel& car, std::string& error);
    void resize(int width, int height);
    // Desenha o carro girado de `yaw` (radianos), a câmera inclinada de `pitch`, a `dist` vezes o raio.
    void draw(const CarModel& car, float yaw, float pitch, float dist);
    void present();
    // Espera a GPU terminar o que foi pedido (uma consulta EVENT): para medir quadros fora da tela.
    void finish();
    bool save_png(const std::wstring& path);
    const DrvRenderStats& stats() const { return stats_; }

private:
    struct Impl;
    Impl* d_;
    DrvRenderStats stats_;
};

}  // namespace drv
