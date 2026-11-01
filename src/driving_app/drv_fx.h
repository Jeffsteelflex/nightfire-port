#pragma once
// O lado do nf_driving dos efeitos (docs/driving.md seção 25): a carga (a galeria data\gfx\data\gall1.gal, o contexto de
// texturas das partículas data\render\partic01.ssh, as texturas 'sprk', 'decl' e 'trai' do common.ssh e o
// data\render\decal.dat da missão), as ligações das explosões e dos eventos com a galeria, as marcas e as luzes, e o
// passo do quadro: o fim do anterior (RLightManager::FinishLights, 0x001c3d58), o RTimeData::Update e o
// RRenderWorldCamera::DrawEffects (0x001c5928, na ordem dele: o Missile::Render de cada míssil pelo DrawBulletStreaks, o
// RDecalManager::Draw, o GFX::Update, o RParticleSystemManager::UpdateSpawnAllSystems e o UpdateAndRenderAllSystems),
// com os lotes do desenho (as partículas, as marcas, os rastros) e as luzes pontuais publicadas na cena.
//
// Divergência (só do port): o PS2 anda os efeitos a cada quadro desenhado (até 3 tiques por quadro); o nf_driving os
// anda a cada passo da simulação (60 por segundo, como o PS2 a 60 quadros), também nas capturas, que só desenham o
// último quadro; o recorte das telas (GFXGallery::CULL_*) roda só nos quadros desenhados, pelo corte do mundo.

#include <cstdint>
#include <string>
#include <vector>

#include "driving/fx/bullet_streak.h"
#include "driving/fx/debris.h"
#include "driving/fx/glare.h"
#include "driving/fx/missile_fx.h"
#include "driving/track.h"

namespace nf {
class DiscSource;
}
namespace drv {
class Bigf;
class DrvRenderer;
struct DrvScene;
}  // namespace drv

struct DrvFxApp {
    // `instance_position`: o RSceneObj::GetInstancePosition do nf_driving. Devolve se a galeria foi achada.
    bool init(drv::Bigf* misc, drv::Bigf* mission, bool (*instance_position)(uint32_t scene, int32_t instance,
                                                                              float m[4][4]));
    // As texturas dos efeitos no renderizador (depois do init dele).
    void textures(drv::Bigf* misc, drv::Bigf* mission, drv::DrvRenderer& renderer);
    // O passo dos efeitos (o DrawEffects): `step` o passo da simulação (um só por passo), a matriz da câmera (as linhas
    // do RCamera: 2 a frente, 3 o olho) e a vista.
    void update(uint32_t step, const float camera[16], const float view[16]);
    // Os lotes do quadro na cena (o fov do RCamera em graus, o aspecto da janela). Seção 29: com a pista e as
    // instâncias desenhadas, os clarões das lâmpadas do mundo (o DrawGroupDrawEffects de cada uma, e um DrawGlares só
    // deles: o PS2 os junta aos da galeria no mesmo quadro).
    void push(drv::DrvScene& scene, float fov, float aspect, const drv::Track* track = nullptr,
              const std::vector<drv::Track::Draw>* draws = nullptr, const float camera[16] = nullptr);
    std::string report() const;
    std::string extra_report() const;
    // Depuração (--play-effect NOME@s[@m]): o efeito da galeria disparado no segundo s, m metros (padrão 15) à frente da
    // câmera, 1,5 m abaixo do olho.
    std::vector<std::string> debug_effects;
    // Depuração (--spawn-explosion s[@m[@dano]]): um ESpawnExplosion (o evento 33, como o do ESpawnExplosionStatic) no
    // segundo s, m metros (padrão 15) à frente da câmera no chão sob ela (o olho menos 1,5 m), com o dano (padrão 500).
    std::vector<std::string> debug_explosions;
    uint64_t debug_fired = 0, debug_missing = 0;
    bool ready = false, have_gallery = false;
    int texture_count = 0;
    uint32_t last_step = 0xffffffffu;
    uint64_t updates = 0, batches = 0, triangles = 0;
    // Os rastros dos mísseis do último quadro (o Missile::Render), para o push.
    std::vector<drv::StreakStrip> streaks;
    int decal_types = 0;  // os tipos lidos do decal.dat (0 sem ele)
    // Seção 29: a pista (o nome dos .tun), os desenhos do RBulletStreak e os clarões do último passo.
    std::string track;
    const nf::DiscSource* disc = nullptr;  // o DRIVING.ELF: os modelos dos destroços (nulo: sem destroços)
    std::vector<drv::DebrisGeometry> debris_geo;
    std::vector<drv::DebrisDraw> debris_draws;
    bool have_fx_tun = false, have_glare_tun = false;
    std::vector<drv::VolatileDraw> volatile_draws;
    drv::GlareDrawList glare_list, world_glare_list;
    float glare_control_y = 66.0f / (60.0f * 4.0f);
    uint64_t world_glare_instances = 0;
    std::string rest_report() const;
    // As luzes pontuais que o quadro desenha (as publicadas no fim do anterior; o FinishLights fecha o quadro no update).
    drv::PositionalLightBlock frame_lights{};
    // Seção 32: os carros desenhados neste quadro (o RVehicle e a matriz do objeto: o RVehicle::RenderFX de cada um, com
    // os clarões dos canos e os efeitos das instâncias, antes do DrawGlares) e os desenhos com matriz que o nf_driving
    // junta (os clarões das armas dos atores); o push os consome.
    struct FrameCar {
        uint32_t vehicle = 0;  // (p32 RVehicleData)
        float transform[4][4] = {};
    };
    std::vector<FrameCar> frame_cars;
    // Seção 34: os objetos de cena do mundo desenhados pelas cópias do Handle (o RenderEffects do RSceneObj::Render).
    std::vector<uint32_t> frame_scene_objs;
    uint64_t scene_obj_fx_frames = 0;
    std::vector<drv::VolatileDraw> frame_volatile;
    uint64_t car_fx_frames = 0, flash_draws = 0;
    std::string section32_report() const;
};

// O --check do nf_driving (docs/driving.md seção 25): a galeria e os tipos de partícula carregam, os efeitos andam; com
// `verify`, os retângulos e os riscos de efeitos de verdade (todos os da galeria disparados diante de uma câmera) no
// microprograma original do RenderSpritesRot e do MatDebugTexture (o bondrm.o), contra a referência (fx_verify.h), e as
// cópias dos dois métodos no DRIVING.ELF. Imprime o resultado; devolve as falhas.
int drv_fx_check(const nf::DiscSource& disc, bool verify);
