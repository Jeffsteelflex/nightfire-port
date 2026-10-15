#pragma once
// A sombra dos carros no PS2: o RShadowMap do DRIVING.ELF (RDebris.obj), portado do MIPS e conferido contra o original
// no nf_driving_check (parte 12, `--so carro`). docs/driving.md seção 16.
//
// No PS2 a sombra é sempre o quadrado simples sob o carro (RShadowMap::DrawSimpleShadow): o RVehicle::Render o desenha
// direto (com RVehicle::fDoSimpleShadow, ou no carro do jogador quando o +0x350 de um objeto do RRenderer pede) ou pelo
// RVehicle::RenderShadow (o slot 0x2c da tabela virtual, com o carro a menos de 0,125 da distância de vista do artigo),
// que chama o RShadowMap::Draw 0x1ec2d8: DrawSimpleShadow e RRenderSharedData::SendPerViewPort, ou ClearShadowMap sem o
// modelo de sombra (o 'as' do tipo 3). O BondCarShadow (o modelo do 'as' do tipo 3 pelo seu render method) e o volume (o
// do tipo 4) nunca são desenhados: RVehicle::RenderShadowGeometry e RenderShadowVolume não têm quem os chame. A versão de
// Xbox (o nightfire-cxbx, NightfireResearch) desenha uma sombra projetada numa textura; a do PS2, não.
//
// O quadrado (DrawSimpleShadow 0x1ec4a0): as quatro rodas (as alavancas do corpo no mundo mais a posição, a altura de
// cada uma mais o GetWheelRoadHeight), o centro (x e z do corpo, y a média das rodas + 0,1), o nível
// round((2 - min(2, a maior queda)) x 0,5 x (força x 255 >> 8)), os dois eixos (as diferenças das rodas somadas aos
// pares, desistindo com o carro inclinado demais: |y| > 3 ou 3,7) vezes a metade da largura e do comprimento do carro
// ("Shadow width"/"Shadow length", 0,8 por padrão, do banco "Render:FX"), a textura 'cshd' do common.ssh, a cor (nível,
// nível, nível, nível), a mistura Cd - Cs (ALPHA 0x62 com FIX 0x80), sem gravar Z. A força é o "PS2 shadow strength"
// (24; o .data diz 40, o LoadAttributes põe 24 e o banco do disco não muda).

#include <cstdint>
#include <string>

namespace drv {

class BondCar;

struct ShadowQuad {
    bool drawn = false;
    int reason = 0;            // por que não: 1 não é um veículo, 2 o chão sob o centro (normal y < 0,1), 3 o nível,
                               // 4 o eixo do lado inclinado, 5 o da frente
    float corner[4][4] = {};   // a faixa: (-lado -frente), (+lado -frente), (-lado +frente), (+lado +frente)
    float uv[4][4] = {};       // a tabela 0x392728
    uint32_t colour[4] = {};   // o nível nos quatro bytes
    int32_t level = 0;
};

struct ShadowSettings {
    int32_t strength = 0x18;   // "PS2 shadow strength" (0x335388)
    int32_t projected = 1;     // "Enable projected shadows" (0x335384; o PS2 não o lê no carro)
};

// [driving] 0x001eb7f0 RShadowMap::LoadAttributes: o tamanho (0,8 x 0,8) e a força 0x18; o banco "Render:FX" da pista
// muda "Shadowwidth{carro}", "Shadowlength{carro}", "PS2shadowstrength" (os nomes sem os espaços).
void RShadowMap_LoadAttributes(ShadowSettings& s, const std::string& fx_tun, const std::string& car, float& width,
                               float& length);
// [driving] 0x001ec4a0 RShadowMap::DrawSimpleShadow
void RShadowMap_DrawSimpleShadow(const BondCar& car, float width, float length, int32_t strength, ShadowQuad& out);

}  // namespace drv
