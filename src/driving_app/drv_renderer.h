#pragma once
// O renderizador Direct3D 11 do nf_driving (docs/driving.md seção 10): desenha modelos do EAGL decodificados (CarModel:
// os carros e a pista) com os shaders que refazem os microprogramas das famílias MatCar (driving/mat_car.h: o vertex
// shader repete os passos 1-7) e MatBond (driving/mat_bond.h: a cor do vértice, as luzes pontuais, a névoa e o mapa de
// ambiente do Reflect), e o pixel shader faz o que o GS faz: MODULATE/DECAL com o alfa do GS, o teste de alfa com o
// AFAIL, a névoa, as misturas do ALPHA_1.
//
// O que o PS2 faz e o PC faz diferente (só do port): a janela de vista do GS (as constantes 0 e 1 da VU) e o recorte
// por triângulo do microprograma dão lugar à projeção e ao recorte do Direct3D; o Z cresce para longe (o do GS cresce
// para perto: GEQUAL vira LESS_EQUAL); o alfa sai do shader limitado a 1 (o GS aceita até 2 em As = 0xff; os carros só
// usam até 0x80); a passada que falha o teste de alfa com AFAIL "só RGB" vira um segundo desenho sem Z e sem alfa.
//
// Este cabeçalho não traz nada do sistema: o backend é drv_renderer_d3d11.cpp (Direct3D 11); a janela chega como o
// identificador do sistema (Window::native_handle).

#include <string>
#include <vector>

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
    int pass_calls[4] = {0, 0, 0, 0};
    int fallback_calls = 0;
    int instances = 0;  // peças desenhadas (instâncias com modelo)
    int bond_calls = 0;  // desenhos da família MatBond (o mundo)
    std::string adapter;
};

// Uma peça a desenhar numa cena: o modelo (enviado com upload), a peça e a matriz dela no mundo (p' = p x M).
struct DrvItem {
    enum Flags { kNoFog = 1, kNoPointLights = 2, kSky = 4 };
    const CarModel* model = nullptr;
    int part = 0;
    float world[16];
    int flags = 0;
};

// Uma cena: as peças na ordem do desenho, a câmera e as constantes globais da VU1 (as luzes pontuais, a névoa, as
// luzes do carro) que valem para o quadro.
struct DrvScene {
    std::vector<DrvItem> items;
    float view[16];             // o mundo para a câmera (linhas; mão esquerda, z para a frente, y para cima)
    float proj[16];             // a projeção do PC (z de 0 a 1)
    float eye[4] = {0, 0, 0, 0};  // a posição da câmera (GAME::CameraPos)
    CarConstants k;             // as luzes pontuais, a névoa (VU 0x48), as luzes do carro, o especular
    float fog_color[3] = {128, 128, 128};  // o FOGCOL do GS (0..255)
    float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool spec_from_view = true;  // a SpecularMat do carro: a rotação da câmera (o protótipo; ver car_model.h)
};

class DrvRenderer {
public:
    DrvRenderer();
    ~DrvRenderer();
    // window nulo: só o alvo fora da tela (as capturas); senão a janela do sistema (o HWND no Windows).
    bool init(void* window, const DrvRenderOptions& opt, std::string& error);
    // Envia os vértices, os índices e as texturas de um modelo (vários modelos podem ficar enviados ao mesmo tempo).
    bool upload(const CarModel& model, std::string& error);
    void resize(int width, int height);
    // Desenha o carro girado de `yaw` (radianos), a câmera inclinada de `pitch`, a `dist` vezes o raio.
    void draw(const CarModel& car, float yaw, float pitch, float dist);
    // Desenha uma cena (a pista e os carros, com a câmera dada).
    void draw_scene(const DrvScene& scene);
    void present();
    // Espera a GPU terminar o que foi pedido (uma consulta EVENT): para medir quadros fora da tela.
    void finish();
    bool save_png(const std::wstring& path);
    const DrvRenderStats& stats() const { return stats_; }
    int width() const;
    int height() const;

private:
    struct Impl;
    Impl* d_;
    DrvRenderStats stats_;
};

// Matrizes de linha (p' = p x M) para quem monta cenas: a câmera olhando de `eye` para `at` (mão esquerda, y para cima)
// e a perspectiva do PC (z de 0 a 1).
void drv_look_at(const float eye[3], const float at[3], float view[16]);
void drv_perspective(float fov_y_deg, float aspect, float z_near, float z_far, float proj[16]);
void drv_mat_mul(const float a[16], const float b[16], float out[16]);

}  // namespace drv
