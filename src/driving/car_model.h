#pragma once
// Um modelo de carro do EAGL pronto para desenhar no PC (docs/driving.md seção 10): carrega os render methods
// (eaglrm.o, bondrm.o), as texturas (o .ssh do carro, common.ssh, ext.ssh), o CARP do carro com o EAGL::DynamicLoader
// e os construtores do jogo, percorre os modelos (as 43 peças do Vanquish) e as primitivas com o desenho do jogo
// (GeoPrim_Draw), executa a cadeia de DMA no VIF e decodifica cada lote que a VU1 receberia.
//
// Só do port (o que o jogo faz em código que não foi portado): os valores que o RReflection, o RLightManager e o
// RVehicle dão aos símbolos do jogo e às constantes globais da VU1. As propriedades de luz por material vêm do
// data\tuning\Render\CarRender\default.tun como o jogo as lê (RReflection, 0x001c0930: os padrões 0,5/0,5/1,0/0,8 e as
// chaves "[Paint]Ambient{vanquish}"...), mapeadas como o RCARPFile::RegisterSymbols (0x001a8930) as entrega:
// CarLightingGlassProps = tipo 0, Specular 1, Glossy 2 ([Paint]), Dull 3, Chrome 4, Dash 5, em (Ambient, Diffuse,
// Specular, Reflect); ReflectionInfo2 = (Fresnel, ReflectionWarp, SpecularSpot, 1). As texturas GAME::SpecularMap e
// GAME::ReflectionMap: 'carS' do common.ssh (a que o RReflection carrega) e, no lugar do reflexo que o jogo renderiza
// numa textura de 256x256 a cada quadro, o 'bref' do ext.ssh (o reflexo estático da cidade).

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "driving/bigf.h"
#include "driving/eagl_loader.h"
#include "driving/eagl_state.h"
#include "driving/mat_car.h"
#include "driving/carp_runtime.h"
#include "driving/mat_bond.h"
#include "driving/mat_car_verify.h"
#include "driving/mat_char.h"
#include "driving/render/vehicle.h"
#include "driving/shps.h"

namespace drv {

// Os ossos do EAGLAnimationBuffer que o port reserva (o PS2 o aloca do tamanho do maior esqueleto carregado).
constexpr int kCarAnimBufferBones = 128;

// Uma textura como o renderizador do PC a recebe (o TBP0 do TEX0 aponta para ela pelo índice).
struct CarTexture {
    std::string name;    // "$$00", "carS"
    std::string source;  // "vanquish.ssh"
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;  // alfa na escala do arquivo (0x80 = 1,0 nos arquivos do carro)
};

// Um vértice como o vertex shader o recebe: o lote depois do passo 1 do microprograma (car_convert); o shader refaz os
// passos 2-7.
using CarGpuVertex = CarVertexWork;

// Um vértice da família de pele dos personagens (mat_char.h) como o vertex shader o recebe: a posição já pela matriz de
// escala do modelo (o DAT_00348c20 do EAGL::Model::Draw: a paleta da VU é escala x osso, e p x (escala x osso) = (p x
// escala) x osso), a normal pelas três primeiras linhas da escala, o ST, os três ossos do esqueleto e os três pesos.
struct CharGpuVertex {
    float pos[3];
    float normal[3];
    float st[2];
    int32_t bone[4];   // -1: nenhum
    float weight[4];
};
static_assert(sizeof(CharGpuVertex) == 64);

// Um desenho: lotes seguidos com o mesmo método, estado e texturas.
struct CarDraw {
    std::string method;
    int passes = kPassBase;  // máscara de CarPass (as passadas que o microprograma chuta)
    bool fallback = false;   // um método fora da família (ver CarModel::load): só a passada base, com luz da família
    int family = 0;          // 0 a família MatCar (vértices em vertices()), 1 a MatBond do mundo (bond_vertices();
                             // passadas: bit 0 a textura, bit 1 o ambiente, pass[0]/pass[1])
    int part = 0;            // o índice do modelo (a peça)
    uint32_t first_index = 0, index_count = 0;
    float props[4] = {};
    eagl::GsRegs pass[kCarMaxPasses];
    int texture[kCarMaxPasses] = {-1, -1, -1, -1};  // pelo TBP0 de cada passada
    const CarMethodLayout* layout = nullptr;  // o leiaute da família (nulo no substituto e na MatBond): o que cada
                                              // passada calcula (car_pass_stq, car_pass_alpha) e o vidro
    float switch_data[4] = {}, switch_param[4] = {};  // as luzes (MatCarTextureSwitchPacked)
    // A MatBond (family 1): o tipo (BondKind) e o que o passo 3b do microprograma lê do lote (o renderizador calcula o
    // deslocamento da STQ a cada quadro pelo SimStep): os ScrollUV dos Scroll2, o FrameAnim.
    int bond_kind = 0;
    float scroll[2][2] = {};
    float frame_anim[4] = {};
    int switch_index = -1;  // as luzes: o GAME::SwitchData[n] que o lote copia (o desenho troca pelo do quadro)
    // A família de pele dos personagens (family 2: vértices em char_vertices(); passadas: bit 0 a base, 1 o especular, 2 o
    // reflexo): o leiaute, o CharacterOptions (o alfa no w), o ActorLightSources e o ReflectionInfo2 do lote.
    const CharMethodLayout* char_layout = nullptr;
    uint32_t char_options[4] = {};
    float char_lights[7][4] = {};
    float char_refl[4] = {};
};

struct CarPart {
    std::string name;  // "vanquish::{as  0000}::Model[0].00"
    uint32_t address = 0;  // o endereço (p32) do __Model (o que as referências resolvidas do CARP apontam)
    int groups = 0, geoprims = 0;
    uint32_t first_vertex = 0, vertex_count = 0;
    uint32_t first_bond_vertex = 0, bond_vertex_count = 0;
    float bbox_min[3] = {0, 0, 0}, bbox_max[3] = {0, 0, 0};  // no espaço do modelo (sem a matriz da peça)
};

// Uma instância do objeto articulado: um registro de 64 bytes da entrada 'in#0000' do grupo 'Arti' do CARP (o
// CARP::Instance que o RSkeletalObj desenha). As linhas 0-2 são a rotação e a linha 3 a translação (p' = p x M, a
// convenção do EAGL); os w guardam a peça e as bandeiras. A peça é o registro 'as' (40 bytes; a palavra 9 é a
// etiqueta 'sr' com o nome do __Model, "EAGL::vanquish::{as  0000}::Model[0].00"); 'as' de 32 bytes não têm modelo
// (as luzes 'as#002b'/'as#002c'). O Vanquish tem 57: a carroceria em y = 0,696 (a origem do carro fica no chão), as
// rodas em (+-0,748; 0,353; 1,409) e (+-0,748; 0,353; -1,269), as peças de um lado repetidas espelhadas em x.
struct CarInstance {
    int part = -1;             // o índice em CarModel::parts() (-1: um 'as' sem modelo)
    float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    uint32_t flags = 0;        // o w da linha 0 (o bit 0x80 acompanha as cópias espelhadas)
    uint32_t piece = 0;        // o w da linha 1: a etiqueta 'as'
    uint32_t extra[2] = {0, 0};  // os w das linhas 2 e 3
};

struct CarModelStats {
    int models = 0, groups = 0, geoprims = 0, batches = 0, vertices = 0, triangles = 0;
    int fallback_batches = 0, fallback_triangles = 0;
    std::map<std::string, int> method_batches;  // lotes por render method
    std::map<std::string, int> method_triangles;
    int loader_messages = 0, vif_errors = 0, unported_ops = 0, runtime_alloc_unported = 0;
    std::map<int, int> unported_by_op;  // as operações do GeoPrim::Draw puladas, por número
    int dma_null_sources = 0, dma_overflows = 0;  // cópias de referências não resolvidas (zeros) e cadeias grandes demais
    int microcode_switches = 0;
    int char_batches = 0, char_guards = 0, palette_matrices = 0;  // a família de pele (mat_char.h)
    float bbox_min[3] = {1e30f, 1e30f, 1e30f}, bbox_max[3] = {-1e30f, -1e30f, -1e30f};  // das peças nas instâncias
    int instances = 0, instances_without_model = 0, parts_without_instance = 0;
};

class CarModel {
public:
    CarModel();
    ~CarModel();
    // `root` = o disco do jogador (a pasta com DRIVING/, uma imagem .iso, uma unidade: nf::open_disc); `viv` = o arquivo
    // da missão ("MIS01.VIV"); `name` = o carro ("vanquish").
    bool load(const std::string& root, const std::string& viv, const std::string& name, std::string& error);
    bool load(const nf::DiscSource& disc, const std::string& viv, const std::string& name, std::string& error);
    // O mesmo com o CARP e as texturas pelo caminho no arquivo da missão (`crp_key`, `ssh_keys`; o common.ssh e o
    // ext.ssh vêm sempre); `name` é o nome das propriedades de luz ("{vanquish}" do CarRender default.tun).
    bool load_carp(const nf::DiscSource& disc, const std::string& viv, const std::string& crp_key,
                   const std::vector<std::string>& ssh_keys, const std::string& name, std::string& error);
    // Antes do load: o GAME::LightSources (o bloco do RLightManager, light_manager.h) e o GAME::SimStep (o RTimeData) que
    // os lotes do mundo recebem na carga (o DMA os copia para a VU; o que se desenha os pega a cada quadro da cena).
    void set_world_inputs(const float light_sources[7][4], const float sim_step[4]);
    // Um objeto do EAGL partido em .dat e .rel (o arquivo da missão, ou o MISC.VIV), sem CARP: os personagens
    // (data\actors\models\*.dat; docs/driving.md seção 16). Os __Model são desenhados como as peças do carro (os de pele
    // pela família de pele, mat_char.h), sem instâncias.
    bool load_eagl(const nf::DiscSource& disc, const std::string& viv, const std::string& dat_key,
                   const std::string& rel_key, const std::vector<std::string>& ssh_keys, const std::string& name,
                   std::string& error);
    // Antes do load: as texturas de um personagem no lugar das do Actors.ssh (o nome de 4 letras, 'hhhh' 'tttt' 'bbbb',
    // e o .ssh do personagem: a primeira imagem dele), como o ActModel::SetTexture as troca no desenho.
    void set_texture_aliases(std::vector<std::pair<std::string, std::string>> aliases) {
        texture_aliases_ = std::move(aliases);
    }
    // Antes do load: cada lote da família também roda no interpretador da VU1 e é comparado (mat_car_verify.h).
    void set_verify(bool on) { verify_ = on; }
    const CarVerifyStats& verify_stats() const { return verify_stats_; }
    // A família MatBond (mat_bond.h) conferida à parte.
    const CarVerifyStats& bond_verify_stats() const { return bond_verify_stats_; }
    // A família de pele dos personagens (mat_char.h) conferida à parte.
    const CarVerifyStats& char_verify_stats() const { return char_verify_stats_; }
    // Antes do load: o CARP é desserializado e resolvido como o RCARPFile::Resolve faz (carp_runtime.h): os símbolos
    // "CARP", "EAGL" e "CHAR", depois as referências por etiqueta, antes do Resolve do modelo. As pistas precisam disso
    // (as instâncias do mundo apontam os artigos e os LODs); o carro do protótipo não (lê o CARP sem mexer).
    void set_resolve_carp(bool on) { resolve_carp_ = on; }
    // O CARP na memória (depois do load com set_resolve_carp: desserializado e resolvido) e os símbolos que ficaram
    // nulos por espaço de nomes.
    UGroup* carp_root() const;
    const std::map<std::string, int>& carp_misses() const { return carp_misses_; }
    // A peça cujo __Model fica no endereço (p32) `model`; -1 se nenhuma.
    int part_of_model(uint32_t model) const;
    // O CARP como foi lido (os bytes e a árvore de grupos), para quem lê o resto do objeto articulado.
    const uint8_t* carp_data() const;
    size_t carp_size() const;
    const Carp& carp_dir() const;
    // O que o RVehicle::PostLoad lê do modelo (driving/render/vehicle.h): o 'Skel', o BaseDesc, as instâncias e as peças
    // dos 'as' de sombra (o byte +0x1e: 3 e 4) do primeiro 'Arti' do arquivo.
    bool vehicle_info(VehicleModelInfo& out) const;

    const std::vector<CarGpuVertex>& vertices() const { return vertices_; }
    const std::vector<uint32_t>& indices() const { return indices_; }
    // Os vértices e índices da família MatBond (os desenhos com family = 1).
    const std::vector<BondVertexWork>& bond_vertices() const { return bond_vertices_; }
    const std::vector<uint32_t>& bond_indices() const { return bond_indices_; }
    // Os vértices e índices da família de pele (os desenhos com family = 2).
    const std::vector<CharGpuVertex>& char_vertices() const { return char_vertices_; }
    const std::vector<uint32_t>& char_indices() const { return char_indices_; }
    // O maior osso que os lotes de pele usam + 1 (o tamanho da paleta que o desenho precisa).
    int char_bones() const { return char_bones_; }
    const std::vector<CarDraw>& draws() const { return draws_; }
    const std::vector<CarTexture>& textures() const { return textures_; }
    const std::vector<CarPart>& parts() const { return parts_; }
    // As instâncias do 'Arti' (vazio se o CARP não tem: então cada peça é desenhada uma vez, sem matriz).
    const std::vector<CarInstance>& instances() const { return instances_; }
    // A lista de desenho de uma vista (RSceneObj::GetViewDrawList: a entrada 'vd' + vista do 'Arti', u16 índices de
    // instância): no Vanquish vd#0001 tem 47 (sem o painel, os ponteiros e o retrovisor interno: a vista de fora),
    // vd#0002 30 (sem a carroceria nem as rodas: a de dentro), vd#0003-0005 55. Vazia se a vista não existe.
    std::vector<uint16_t> view_draw_list(unsigned view) const;
    int view_count() const { return int(view_lists_.size()); }
    // A caixa do carro montado numa vista (as caixas das peças pelas matrizes das instâncias da lista; vista 0 ou sem
    // lista: todas as instâncias; sem instâncias: as peças como estão).
    void bounds(unsigned view, float lo[3], float hi[3]) const;
    const CarModelStats& stats() const { return stats_; }
    const CarConstants& constants() const { return constants_; }
    CarConstants& constants() { return constants_; }
    // Os lotes decodificados (para a referência na CPU e os testes), na ordem do desenho.
    const std::vector<CarBatch>& batches() const { return batches_; }
    const std::vector<std::string>& messages() const { return messages_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<CarGpuVertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<BondVertexWork> bond_vertices_;
    std::vector<uint32_t> bond_indices_;
    std::vector<CharGpuVertex> char_vertices_;
    std::vector<uint32_t> char_indices_;
    int char_bones_ = 0;
    std::vector<CarDraw> draws_;
    std::vector<CarTexture> textures_;
    std::vector<CarPart> parts_;
    std::vector<CarInstance> instances_;
    std::vector<std::vector<uint16_t>> view_lists_;  // [vista]: as 'vd#vista' (a 0 fica vazia)
    std::vector<CarBatch> batches_;
    std::vector<std::string> messages_;
    CarModelStats stats_;
    CarConstants constants_;
    std::string split_dat_, split_rel_;  // load_eagl: o .dat e o .rel no lugar do CARP
    std::vector<std::pair<std::string, std::string>> texture_aliases_;
    bool verify_ = false;
    CarVerifyStats verify_stats_;
    CarVerifyStats bond_verify_stats_;
    CarVerifyStats char_verify_stats_;
    bool resolve_carp_ = false;
    std::map<std::string, int> carp_misses_;
    std::map<uint32_t, int> part_by_model_;
};

// As propriedades de luz de um carro pela tabela de ajuste (as linhas "chave{carro}=valor"); `out` tem 7 COORD4: os
// tipos 0-5 e o ReflectionInfo2.
void car_lighting_from_tuning(const std::string& tun_text, const std::string& car, float out[7][4]);

// As luzes da pista (data\tuning\Render\Lighting\<pista>.tun, o conjunto "{World}") nas constantes da VU1 que o
// microprograma lê (0x49-0x4f): Light1 e Light2 com os ângulos (graus: Y em volta do eixo vertical, X de inclinação),
// AmbientSky como o ambiente, a terceira luz apagada. Só do port: o RLightManager do jogo (que monta o LightSources do
// MatCarPerCar a cada quadro) não foi lido; a direção usada é a que aponta para a luz, (cos X sen Y, sen X, cos X cos Y)
// com o sinal trocado.
void car_lights_from_tuning(const std::string& tun_text, CarConstants& k);

}  // namespace drv
