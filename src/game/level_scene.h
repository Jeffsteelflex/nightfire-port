#pragma once
// Monta uma fase a partir do FILES.BIN: entidades posicionadas pelo bloco 0x1A do mapa principal,
// com malhas compartilhadas entre instâncias (mesma lógica de tools/geometry.py export_level).

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "data/files_bin.h"
#include "data/map_item.h"

namespace nf {

struct SceneMesh {
    EntityMesh mesh;      // submesh.texture já convertido para o índice global de textures
    float bmin[3]{}, bmax[3]{};  // caixa nas coordenadas locais
};

// Categoria do objeto posicionado, pelo tipo (byte baixo das flags do bloco 0x1A), conforme
// parsemap_create_dynamic_objects (0x001d04f0).
enum class ObjectKind : uint8_t {
    Visible,  // cenário e objetos desenhados pelo jogo
    Sky,      // 0x2A View_AddSkyObj
    Volume,   // gatilhos, volumes e marcadores de edição: têm malha, mas o jogo não desenha
};

ObjectKind classify_object(uint32_t flags);
const char* object_type_name(uint8_t type);

struct SceneInstance {
    int mesh = -1;
    std::array<float, 16> world{};  // vetor-linha (convenção do motor)
    uint32_t flags = 0;
    ObjectKind kind = ObjectKind::Visible;
};

struct SceneTexture {
    const Texture* texture = nullptr;
};

struct LevelStats {
    int placements = 0;
    int placed = 0;
    int without_geometry = 0;
    int skinned_skipped = 0;
    int volumes = 0;
    int unresolved = 0;
    size_t triangles = 0;
};

class LevelScene {
public:
    // level: hash do pacote principal, ex. "07000001"
    LevelScene(const FilesBin& files, const std::string& level);

    const std::string& level() const { return level_; }
    const std::vector<SceneMesh>& meshes() const { return meshes_; }
    const std::vector<SceneInstance>& instances() const { return instances_; }
    const std::vector<SceneTexture>& textures() const { return textures_; }
    const LevelStats& stats() const { return stats_; }

    // Ponto e direção iniciais da câmera: centro mediano das instâncias, ignorando o céu.
    void suggested_view(float eye[3], float target[3]) const;

private:
    struct EntityRef {
        const MapItem* item;
        size_t entity;
    };
    int mesh_for(const MapItem& item, size_t entity);

    std::string level_;
    std::vector<std::unique_ptr<MapItem>> items_;
    std::vector<EntityRef> entity_refs_;  // valores registrados na tabela de hashcodes
    std::vector<SceneMesh> meshes_;
    std::vector<SceneInstance> instances_;
    std::vector<SceneTexture> textures_;
    std::vector<std::pair<std::pair<const MapItem*, size_t>, int>> mesh_cache_;
    std::vector<std::pair<std::pair<const MapItem*, int>, int>> texture_cache_;
    LevelStats stats_;
};

// Caixa de um objeto visível no mundo, para escolher a câmera inicial.
struct WorldBox {
    float lo[3], hi[3];
};
WorldBox transform_box(const float bmin[3], const float bmax[3], const std::array<float, 16>& world);
// Ponto e direção iniciais da câmera: centro mediano das caixas, ignorando o céu e os objetos
// muito afastados do resto.
void suggest_view(const std::vector<WorldBox>& boxes, float eye[3], float target[3]);

// Hashes dos pacotes que têm mapa principal, na ordem do FILES.BIN.
std::vector<std::string> list_levels(const FilesBin& files);

}  // namespace nf
