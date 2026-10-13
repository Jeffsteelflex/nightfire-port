#pragma once
// A pista de uma missão de carro (docs/driving.md seções 5 e 12): o CARP da pista (data\track\<pista>.crp) carregado e
// resolvido como o WWorld::LoadTrackFile e o RCARPFile::Resolve fazem, com os modelos do EAGL decodificados (CarModel,
// as famílias MatCar e MatBond) e o que o desenho do mundo lê: as instâncias do grupo 'Map ' ('in  ', CARP::Instance),
// cada uma com o artigo ('as' resolvido: os LODs, a passada do WRender, o tipo de render) e as cinco passadas do
// WRender::WRender (0x0021eae8).
//
// O nome da pista ("paris_mis01") dá o arquivo da missão: o que vem depois do último '_' ("mis01" -> MIS01.VIV;
// GameLoop_StartUsingMainBigFile, 0x00175a28).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "driving/car_model.h"
#include "platform/disc_source.h"

namespace drv {

// Um artigo do mundo (o 'as' que as instâncias apontam): os modelos por distância, a passada, o tipo de render.
struct TrackArticle {
    uint32_t address = 0;    // o registro resolvido (p32)
    std::string name;        // o nome do grupo 'Arti' (o 'Name')
    struct Lod {
        float distance = 0;  // até onde o modelo vale (o último: também além, com a bandeira 8 da instância)
        int part = -1;       // a peça do CarModel (-1: o modelo não foi achado)
    };
    std::vector<Lod> lods;
    int draw_pass = 0;       // +0x1d
    int render_type = 0;     // +0x1e 0 mundo, 1 carro, 2 objeto
    int render_flags = 0;    // +0x1f 1: sem névoa
};

// Uma instância de render do mundo (CARP::Instance do 'in  ' do 'Map ').
struct TrackInstance {
    float matrix[16];        // linhas 0-2 a rotação, 3 a posição (os w zerados; o w da linha 3 é 1)
    uint8_t flags = 0;       // +0x0c: 4 céu, 8 sem corte no longe, 0x40, 1/0x10 objeto de cena/animação (WWorld)
    int article = -1;        // índice em Track::articles() (-1: sem artigo)
    int pass = 0;            // a passada do WRender (4: o céu, desenhado pelo RSky)
    float radius = 0;        // o raio das medidas empacotadas (GetInstanceViewDistance)
    uint32_t packed_dims = 0;
};

struct TrackStats {
    int instances = 0, articles = 0, lods = 0, lods_without_model = 0, instances_without_article = 0;
    int pass_count[5] = {0, 0, 0, 0, 0};
};

class Track {
public:
    Track();
    ~Track();
    // `viv` vazio: o nome tirado da pista ("paris_mis01" -> "MIS01.VIV").
    bool load(const nf::DiscSource& disc, const std::string& track, const std::string& viv, std::string& error);
    void set_verify(bool on) { model_.set_verify(on); }

    const CarModel& models() const { return model_; }
    CarModel& models() { return model_; }
    const std::vector<TrackInstance>& instances() const { return instances_; }
    const std::vector<TrackArticle>& articles() const { return articles_; }
    const TrackStats& stats() const { return stats_; }
    const std::string& name() const { return name_; }
    const std::string& viv() const { return viv_; }
    UGroup* root() const { return model_.carp_root(); }
    // O grupo 'Map ' da pista (ou nulo).
    UGroup* map_group() const;

    // [driving] 0x0007dc40 (Xbox; a função do PS2 que o RRenderWorld usa) GetInstanceViewDistance: a distância da
    // câmera ao centro da instância menos o raio (no mínimo 0), vezes o multiplicador de LOD da vista.
    static float view_distance(const TrackInstance& inst, const float eye[3], float lod_multiplier = 1.0f);
    // A peça do LOD para a distância (DrawArticleModel): o primeiro LOD cuja distância cobre; além do último, nada
    // (-1), salvo a bandeira 8 da instância (o primeiro LOD a qualquer distância).
    int lod_part(const TrackInstance& inst, float distance) const;

    // O que uma câmera vê (o papel do WRender: as passadas, o LOD, o corte): para cada instância com modelo, a peça do
    // LOD e a distância, sem as que ficam fora do volume de visão `view_proj` (p' = p x M, z de 0 a 1; a esfera da caixa
    // da peça pela matriz da instância). O céu (passada 4) vai sempre. Só do port: o jogo corta pela árvore de cenas
    // ('Wmap'/'wn') e pelas cortinas de visibilidade ('VisC'), que não foram portadas; aqui o corte é só o do volume.
    struct Draw {
        int instance = 0, part = -1, pass = 0;
        float distance = 0;
    };
    // `lod_multiplier`: o multiplicador de LOD da vista (RViewCamera::RefreshLODMultiplier 0x001d44c8: um inteiro, (o
    // parâmetro +0xa4 da câmera / 33 / 400) x a largura da vista, truncado). Todos os artigos de Paris têm um LOD só,
    // com distância 1,0: com o multiplicador 1 ou mais o mundo sumiria além de meio metro do raio de cada instância, então
    // no jogo ele vale 0 (o +0xa4 em radianos: (0,5 / 33 / 400) x 640 trunca para 0); a conferir no PCSX2.
    void visible(const float view_proj[16], const float eye[3], std::vector<Draw>& out, float lod_multiplier = 0.0f) const;
    // A caixa das instâncias do mundo sem o céu (para escolher uma câmera).
    void bounds(float lo[3], float hi[3]) const;

private:
    CarModel model_;
    std::vector<TrackInstance> instances_;
    std::vector<TrackArticle> articles_;
    TrackStats stats_;
    std::string name_, viv_;
};

// O .VIV de uma pista pelo nome (GameLoop_StartUsingMainBigFile: o que vem depois do último '_', em maiúsculas).
std::string track_viv_name(const std::string& track);

}  // namespace drv
