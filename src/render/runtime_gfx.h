#pragma once
// Liga as estruturas do jogo ao renderizador: implementa o backend de psi_gfx (texturas e malhas
// criadas durante o ResetMap_Load) e converte o quadro registrado por psi_draw (o que o Game_Draw
// pediu para a câmera 0) em itens do renderizador, mais os objetos provisórios (tipos de objeto
// ainda não portados, que o jogo não conhece e por isso não passam pela visibilidade).
//
// Malhas com skinning viram malhas da GPU na pose de repouso; só são desenhadas pelos registros de
// psiDrawSkinObjectMatrix (com a paleta copiada para o DrawItem), nunca como malha rígida. Os rostos levam os
// alvos de morph da entidade (bloco 0x2B) para a GPU, e cada desenho deles, os pesos (DrawItem::morph).
//
// O 2D (psiDrawSprites) entra de todas as câmeras, na ordem do Game_Draw, já levado para a janela
// (platform/psi_2d.h, psi_2d_viewport); com a camada HD ligada (set_ui_hd), refeito por ela (render/ui_hd.h).

#include <string>
#include <vector>

#include "engine/map_types.h"

struct viewer_tag;
#include "data/texture_pack.h"
#include "platform/psi_2d.h"
#include "render/renderer.h"

namespace nf {

struct UiHdConfig;

// Só do port: um segundo caminho de desenho que recebe uma cópia dos recursos da fase (as texturas por slot de Tex e
// as malhas pelo mesmo id do Renderer), criados uma vez na carga. Usado pelo protótipo de Direct3D 9 de função fixa
// para o RTX Remix (render/remix_d3d9.h, docs/remix.md). As substitutas HD do pacote do PCSX2 não vão para ele.
class GfxMirror {
public:
    virtual ~GfxMirror() = default;
    virtual void mirror_reset() = 0;
    virtual void mirror_texture(int slot, const Texture& texture) = 0;
    virtual void mirror_mesh(int id, const EntityMesh& mesh) = 0;
};

// Só do port (o relatório das capturas e o --frames-dump): os rostos desenhados no quadro (itens com skinning de
// objetos cuja pele tem morphs), pela distância à câmera `eye`. `line` recebe a linha "rosto ..." do relatório
// (quantos rostos, quantos com pesos, e dos 4 mais perto: distância, posição, tipo e pele do objeto, estado do
// rosto, timer da piscada, olhos, scripts de rosto 0xe0000000/1/2 na lista e os pesos desenhados acima de 0,005);
// `columns`, as colunas do frames.tsv do rosto com pesos mais perto (ou do mais perto): distância, pele, estado e
// os 22 pesos desenhados separados por vírgula (vazias sem rosto).
void describe_faces(const std::vector<DrawItem>& items, const float eye[3], std::string* line, std::string* columns);

class RuntimeGfx {
public:
    explicit RuntimeGfx(Renderer& renderer);
    ~RuntimeGfx();
    RuntimeGfx(const RuntimeGfx&) = delete;
    RuntimeGfx& operator=(const RuntimeGfx&) = delete;

    // Tudo o que a fase tem para desenhar (salas, células, objetos, provisórios), sem visibilidade:
    // para saber se o pacote tem cenário e escolher a câmera inicial.
    void collect(std::vector<DrawItem>& out) const;
    // O quadro do Game_Draw para a câmera "camera", mais os objetos provisórios.
    void collect_frame(const viewer_tag* camera, std::vector<DrawItem>& out) const;
    // Só do PC: quadrados 2D avulsos na tela do PS2 (as bolinhas da tela de carga) levados para a janela como os do
    // registro (psi_2d_viewport, o desenho original), acrescentados a out.
    void collect_2d(const Psi2DQuad* quads, uint32_t count, std::vector<DrawItem>& out) const;
    void suggested_view(const std::vector<DrawItem>& items, float eye[3], float target[3]) const;
    size_t triangles(const std::vector<DrawItem>& items) const;

    // Pacote de texturas HD (pode ser nulo): consultado a cada textura criada.
    void set_texture_pack(const TexturePack* pack) { pack_ = pack; }
    // Camada 2D em alta definição (render/ui_hd.h): com uma configuração, o 2D vira lotes dela; nula, o
    // desenho original.
    void set_ui_hd(const UiHdConfig* config) { ui_hd_ = config; }
    // Só do PC: o 2D das câmeras do jogo que some do quadro (bit i = glb_viewer[i]; ~0 todo o 2D): os menus no estilo
    // Modern escondem o desenho do menu do jogo (câmera 7) e, na pausa, o HUD (render/ui_modern_app.h). 0: tudo.
    void set_hidden_2d(uint32_t cameras) { hidden_2d_ = cameras; }
    // Só do port: o segundo caminho de desenho (nulo: nenhum). Recebe o que for criado depois desta chamada.
    void set_mirror(GfxMirror* mirror) { mirror_ = mirror; }
    bool ui_hd() const { return ui_hd_ != nullptr; }
    int textures() const { return textures_; }
    int hd_textures() const { return hd_; }
    // Malhas com skinning da fase: decodificadas sem problema, e com erro de decodificação (sem malha)
    // ou com lotes descartados (malha só com os lotes bons). O esperado nos dados do jogo é 0 falhas.
    int skinned_meshes() const { return skinned_; }
    int skinned_failed() const { return skinned_failed_; }
    // Rostos da fase: malhas com skinning com os alvos de morph (bloco 0x2B) decodificados, e com o bloco que não
    // se decodificou (ficam sem morph). O esperado nos dados do jogo é 0 falhas.
    int morph_meshes() const { return morph_meshes_; }
    int morph_failed() const { return morph_failed_; }

private:
    struct MeshInfo {
        float bmin[3], bmax[3];
        size_t triangles;
        bool skinned;
        int morph_targets;  // alvos de morph (0: sem rosto)
    };
    static void on_texture(void* user, int index, const bmpheader_tag& header, const uint8_t* pixels,
                           uint32_t pixels_bytes, const uint8_t* palette, uint32_t palette_bytes);
    // O jogo reescreveu a textura (o fundo animado da pausa): o conteúdo novo vai para o renderizador e o espelho.
    static void on_texture_update(void* user, int index, const bmpheader_tag& header, const uint8_t* pixels,
                                  uint32_t pixels_bytes, const uint8_t* palette, uint32_t palette_bytes);
    static int on_entity(void* user, const celglist_tag* glist, const map_tag* map);
    static void on_reset(void* user);
    // Malha da entidade para desenho rígido (-1 se não houver ou se for com skinning).
    int rigid_mesh(const celglist_tag* glist) const;

    Renderer& renderer_;
    std::vector<MeshInfo> meshes_;
    int textures_ = 0;
    int hd_ = 0;
    const TexturePack* pack_ = nullptr;
    const UiHdConfig* ui_hd_ = nullptr;
    uint32_t hidden_2d_ = 0;
    GfxMirror* mirror_ = nullptr;
    int skinned_ = 0;
    int skinned_failed_ = 0;
    int morph_meshes_ = 0;
    int morph_failed_ = 0;
    Texture updated_;  // o último quadro de on_texture_update (a memória fica: nada é alocado a cada quadro)
};

}  // namespace nf
