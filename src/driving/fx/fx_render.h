#pragma once
// O desenho dos efeitos (docs/driving.md seção 25): os dois render methods que as partículas, as marcas de queimado e os
// rastros usam, lidos dos microprogramas da VU1 (o DRIVING.ELF traz uma cópia dos dois na .data: RenderSpritesRot em
// 0x34cc20 e MatDebugTexture em 0x34a280; os mesmos do bondrm.o do MISC.VIV), e a passagem para o desenho do PC.
//
// RenderSpritesRot (os retângulos girados das partículas; os parâmetros Parameters, ModelViewMatrix, Texture,
// Coordinates, UVUVs, Colours, State, Control): por retângulo, c = (x, y, z, 1) x MVP (TOP+1..4), Q = 1/c.w, a tela =
// VU 0 + c.xyz x VU 1 x Q; o raio na tela r = tamanho x Q x (VU 1 x Control.y) nos dois eixos; o giro (o w do UVUV)
// pelo seno da EFU (ESIN de g e de g + 1,5708 = o cosseno): v1 = (cos, sen) x r, v2 = (-sen, cos) x r, os cantos
// c - v2 (ST u, v), c + v1 (u + lado, v), c - v1 (u, v + lado), c + v2 (u + lado, v + lado), seis vértices (dois
// triângulos); fora do recorte (CLIPw) ou com z x VU1.z + tamanho x Control.z >= 0, o w do XYZ é 2048 (o ADC: não
// desenha). Control (o JustRender): y = 66 / (o fov da câmera x a margem de guarda da vista, 4), z = y x 1,4e7.
// No PC o raio em coordenadas normalizadas é Control.y x tamanho / w nos dois eixos, isto é, um deslocamento constante
// no recorte (Control.y x tamanho); o x vezes (4/3) / o aspecto da janela, para o retângulo ter na tela a proporção
// que tem na tela 4:3 do PS2 (a projeção do port é Hor+ com o vertical do PS2).
//
// MatDebugTexture (os triângulos com textura, STQ e cor por vértice; o recorte de triângulos na VU1 quando cruzam os
// planos): por vértice c = p x MVP, Q = 1/c.w, a tela como acima, STQ x Q. No PC os vértices vão no mundo e o
// Direct3D recorta (a divergência de sempre).

#include <cstdint>
#include <vector>

#include "driving/fx/particles.h"

namespace drv {

struct FxVertex {
    float pos[4];
    float uv[2];
    uint32_t colour = 0x80808080u;  // RGBA do GS (0x80 = 1,0)
};
struct FxBatch {
    int texture = -1;
    bool world = false;
    uint64_t alpha = 0x8000000044ull;
    uint64_t test = 0x50002ull;
    bool zwrite = true;
    std::vector<FxVertex> verts;
};
// As texturas dos efeitos: o 'MAIN' do contexto 1 (data\render\partic01.ssh, RParticleSystemManager::Init) e os do
// contexto 0 (o common.ssh): 'sprk' (os riscos), 'decl' (as marcas), 'trai' (o rastro dos mísseis).
enum FxTexture { kFxTexMain = 0, kFxTexSpark = 1, kFxTexDecal = 2, kFxTexTrail = 3 };

// O Control.y do RenderSpritesRot (0x001f4920 RParticleParticleCache::JustRender): 66 / (fov x margem).
float fx_sprites_control_y(float fov, float guard_band);

// As listas do RenderBlock em lotes do PC: os retângulos normais (o GeoPrimState 0x3e3f70: (Cs - Cd) As + Cd), os
// aditivos (0x3e5ff0: Cs As + Cd) e os riscos (0x3e8070: Cs FIX + Cd, FIX 0x80; o STQ fixo do construtor do cache).
void fx_particle_batches(const ParticleDrawLists& lists, const float view[16], const float proj[16], float control_y,
                         float aspect, std::vector<FxBatch>& out);

// ---------------------------------------------------------------------------------- as referências (a conferência)

// A saída de um vértice do GS como o microprograma a monta (o XYZ em ponto fixo 12.4 com o w, o ST/RGBA em floats ou
// inteiros, conforme o método).
struct GsVertexOut {
    uint32_t st[4];    // RenderSpritesRot: (s, t, 1, 0) em floats; MatDebugTexture: STQ x Q
    uint32_t rgba[4];  // os inteiros 0..255 (o V4-8 desempacotado)
    uint32_t xyz[4];   // FTOI4 (x, y, z, w)
};
// As constantes globais da VU1 que os dois leem: a janela de vista (VU 0 o deslocamento, VU 1 a escala).
struct FxVuGlobals {
    float vp_offset[4] = {2048, 2048, 0, 0};
    float vp_scale[4] = {320, -224, -16777215, 0};
};
// RenderSpritesRot para um retângulo: os seis vértices na ordem do microprograma, com a aritmética da VU (o ESIN pela
// EFU do port). `control` = o qword do Control (y e z), `mvp` = as linhas TOP+1..4.
void render_sprites_rot_reference(const FxVuGlobals& g, const float mvp[4][4], const float control[4],
                                  const float coord[4], const float uvuv[4], const uint32_t colour[4],
                                  GsVertexOut out[6]);
// MatDebugTexture para um vértice que não cruza os planos (o caminho sem recorte do microprograma).
void mat_debug_texture_reference(const FxVuGlobals& g, const float mvp[4][4], const float pos[4], const float stq[4],
                                 const uint32_t colour[4], GsVertexOut& out);

}  // namespace drv
