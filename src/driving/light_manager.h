#pragma once
// As entradas por quadro dos render methods do mundo que não são da câmera (docs/driving.md seção 15):
//  - o RTimeData (0x334000): o GAME::SimStep dos métodos que animam a textura (MatBondScrollTexture2, o
//    MatBondFrameAnimPacked; mat_bond.h): o x é o passo da simulação em float, atualizado no começo de cada desenho
//    (RTimeData::Update, chamado do StartFrameRender);
//  - o bloco de luzes do RLightManager (0x70 bytes): o GAME::LightSources (o RLightManager + 0x7c) que o
//    MatDynamicTexturePacked lê no TOP+0x10..0x16: as três direções transpostas nas linhas 0-2 (a coluna k é a luz k:
//    a Light 1, a Light 2 e a "Dual Hemi", que o RefreshLightBlocks põe apontando para cima no mundo e nos personagens)
//    e as cores na ordem Light 1, Light 2, Ambient Ground (a da Dual Hemi) e Ambient Sky (o ambiente). Os quatro
//    conjuntos ("Current Environment": World, Character, Unused1, Unused2) vêm do data\tuning\Render\Lighting\<pista>.tun
//    da missão; o SetLightingModel copia o do modelo (0 e 1 o World, 2 o Character) para o LightSources.
// Divergências (só do port): o DTuningDB da EA (dbindex/dbattrib_argb/dbattrib_float, DTuningFile::ParseData) não foi
// portado: as chaves do .tun ("Light1{World}=0.9,0.94,1.0,0", "Light1AngleX{World}=-51.3") são lidas direto do texto
// com o strtod do C (o sscanf "%f" do newlib); o sinf/cosf do newlib do BuildRotate é o nf::fm::sin/cos, como na colisão.

#include <cstdint>
#include <string>

namespace drv {

// RTimeData (os globais fTimeData 0x334000, fTime 0x334010, fCausticData 0x334030 e o 0x33403c).
struct RTimeData {
    float time_data[4] = {0.0f, -0.002f, 0.0f, 0.0f};  // o .data: (0, -0,002, 0, 0)
    uint32_t time = 0;
    float caustic[4] = {0.014f, 0.45f, 0.54f, 0.64f};
    float caustic_time = 0.0f;  // 0x33403c
};
RTimeData& rtime_data();
// [driving] 0x001d1ef8 RTimeData::Update(void): o passo da simulação (Simulation + 0xdc4) sem sinal em float.
void RTimeData_Update(int32_t sim_steps);

// O bloco de luzes (0x70 bytes, como a VU1 o recebe).
struct LightBlock {
    float dir[3][4];     // +0x00 as direções transpostas (a coluna k é a luz k)
    float colour[4][4];  // +0x30 Light 1, Light 2, Ambient Ground, Ambient Sky
};
static_assert(sizeof(LightBlock) == 0x70, "LightBlock");

// O que o port tem do RLightManager: os quatro blocos (+0x80, de 0x70 em 0x70), os ângulos de cada conjunto (+0x240,
// de 0x18 em 0x18: X da Light 1, X da Light 2, X da Dual Hemi, Y da Light 1, Y da Light 2, Y da Dual Hemi, em graus),
// o modelo (+0x74) e o LightSources (*(+0x7c)).
struct RLightManagerLights {
    LightBlock blocks[4] = {};
    float angles[4][6] = {};
    int model = -1;
    LightBlock light_sources = {};
};

// [driving] 0x002d11c8 BuildRotate(MATRIX4 &, float, float, float, float): a rotação de `degrees` em volta do eixo
// (normalizado pelo RSQRT.S); 0 graus dá a identidade.
void BuildRotate(float m[16], float degrees, float x, float y, float z);
// [driving] 0x002d1920 TransformPoint(MATRIX4 &, COORD4 &, COORD4 &): out = in x m (VU0 em macro, as quatro pistas).
void TransformPoint(const float m[16], const float in[4], float out[4]);
// [driving] 0x001b0038 (sem nome no SYM; o nightfire-cxbx a chama SetLightDirections): as três direções de um bloco, (0,
// 0, 1) girado pelo ângulo X em volta de x e depois pelo Y em volta de y.
void RLightManager_SetLightDirections(LightBlock& block, const float angles[6]);
// [driving] 0x001b0158 RLightManager::RefreshLightBlocks(void)
void RLightManager_RefreshLightBlocks(RLightManagerLights& m);
// A parte do construtor (0x001ae630) que lê o "Render:Lighting" (as cores e os ângulos dos quatro conjuntos) e o
// RefreshLightBlocks do fim; o texto do .tun (vazio: tudo zero).
void RLightManager_Construct(RLightManagerLights& m, const std::string& tun_text);
// [driving] 0x001aec20 RLightManager::SetLightingModel(int): o bloco do modelo copiado para o LightSources.
void RLightManager_SetLightingModel(RLightManagerLights& m, int model);

}  // namespace drv
