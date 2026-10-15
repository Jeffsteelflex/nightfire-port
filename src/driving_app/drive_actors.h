#pragma once
// Os atores no nf_driving (docs/driving.md seção 24): o que o GameLoop_StartUp monta para eles e o desenho. O port
// carrega os recursos que o ActManager::ActManagerInit e o ActCharacter::LoadCharacter carregariam (os esqueletos 0-8,
// os bancos de animação da lista ACT_ANIM_BANK da coleção "world" da pista ou o BkDflt.txt, o ALookup.bin, os modelos
// dos personagens e o CARP de cada arma com o RAnimEngine::Handle das instâncias), e desenha o que o
// ActActorDatabase::DrawAll e o DrawAllWeapons desenhariam: cada ator visível com a família de pele (o EAGLAnimationBuffer
// do ActPoser::Skin) e as armas pelas instâncias (RSceneObj::GetInstancePosition do ActWeapon).
//
// Divergência do desenho: o ActActor::SetupFOVConversion (0x001092f8) desenha os atores da câmera (o +0x60: as mãos) com
// o campo de visão vezes o +0x5c (0,5) e outro desvio de profundidade; aqui a mesma projeção sai da matriz do mundo (o
// espaço da vista escalado em x e y por tan(fov) / tan(fov x escala)), sem o desvio de profundidade.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "driving_app/drv_renderer.h"

namespace nf {
class DiscSource;
}

namespace drv {
class Bigf;
class AttributeCollection;

namespace drive_actors {

// Antes do SWeaponManager (o GameLoop_StartUp: o ActActorDatabase 0x0010b228 e o ActManager::StartUp(1 / a taxa do
// vídeo)): os recursos e o ActManager. `anim_list` o nome do ACT_ANIM_BANK ("" o BkDflt.txt).
bool startup(const nf::DiscSource& disc, const std::string& viv, Bigf& arc, const std::string& anim_list,
             float frame_time, std::string& error);
// O AICharacter::Init (depois do SWeaponManager::SetPlayerCar): os ganchos das mãos (o RPlayerCamera do jogador no
// leiaute do PS2 e o UpdateCamera dele, o fgZoomButton, os vetores AIC_* dos atributos do carro do jogador) e os
// personagens.
void characters_init(const AttributeCollection* player_attrs, const uint8_t* camera, std::function<void()> camera_update,
                     int32_t* zoom_button);
// Envia ao renderizador os modelos que os atores carregaram (os personagens e as armas com peças).
bool upload(DrvRenderer& renderer, std::string& error);
// Os desenhos dos atores no quadro: `cam` a matriz da câmera (as linhas: direita, cima, frente, o olho) e `fov` o
// meio campo de visão horizontal (graus) do RPlayerCamera.
void draw(std::vector<DrvItem>& items, const float cam[16], float fov);
// A linha "atores ..." do relatório.
std::string report();
// O fim (os atores, as mãos, o ActManager).
void shutdown();

}  // namespace drive_actors
}  // namespace drv
