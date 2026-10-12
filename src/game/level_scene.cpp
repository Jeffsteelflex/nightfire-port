#include "game/level_scene.h"

#include <algorithm>
#include <cmath>
#include "data/level_pak.h"
#include "engine/hashtable.h"

namespace nf {

namespace {

void transform_point(const std::array<float, 16>& m, const float in[3], float out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = in[0] * m[c] + in[1] * m[4 + c] + in[2] * m[8 + c] + m[12 + c];
}

float median(std::vector<float> v) {
    if (v.empty()) return 0.0f;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

}  // namespace

const char* object_type_name(uint8_t t) {
    switch (t) {
        case 0x0F: return "Drone";
        case 0x19: return "Célula estática";
        case 0x20: return "Break";
        case 0x21: return "Ripples";
        case 0x22: return "Ladder";
        case 0x24: return "Início do jogador";
        case 0x25: return "Spawn multiplayer";
        case 0x26: return "Objeto multiplayer";
        case 0x27: return "GT";
        case 0x28: return "Sensor";
        case 0x29: return "Switch";
        case 0x2A: return "Céu";
        case 0x2B: return "LeafGen";
        case 0x2C: return "Lightning";
        case 0x2D: return "Início do jogador";
        case 0x2E: return "Lock";
        case 0x2F: return "Copter";
        case 0x30: return "Monitor";
        case 0x31: return "FuseBox";
        case 0x32: return "RainBox";
        case 0x33: return "Hint";
        case 0x34: return "GunImp";
        case 0x3A: return "Grapple";
        case 0x3B: return "DynamicObject";
        case 0x3E: return "Wire";
        case 0x41: return "InverterTrigger";
        case 0x47: return "Célula estática";
        case 0x4A: return "Apocalypse";
        case 0x4E: return "Light";
        case 0xD8: return "RB";
        case 0xDA: return "Car";
        case 0xDB: return "Door";
        case 0xDC: return "Trigger";
        case 0xDE: return "Searchlight";
        case 0xDF: return "Env";
        case 0xE1: return "Destroy";
        case 0xE2: return "SS";
        case 0xE4: return "Hurt";
        case 0xE5: case 0xE6: case 0xE9: return "Nó de IA";
        case 0xE8: case 0xEA: case 0xEB: case 0xEC: case 0xED: case 0xEE: case 0xEF: return "Trigger";
        case 0xF0: return "Pickup";
        case 0xF1: return "DroneSpawner";
        case 0xF3: return "Env_Tree";
        case 0xF4: return "MoviePlayer";
        case 0xF9: return "SoundTrigger";
        case 0xFA: return "ThirdIcon";
        case 0xFB: return "MusicTrigger";
        case 0xFC: return "Corona";
        case 0xFD: return "CamSubject";
        case 0xFE: return "Mine";
        default: return "?";
    }
}

ObjectKind classify_object(uint32_t flags) {
    switch (flags & 0xFF) {
        case 0x2A:
            return ObjectKind::Sky;
        case 0x24: case 0x25: case 0x2D:  // pontos de início e de spawn
        case 0x32:                        // RainBox: volume de partículas de chuva
        case 0x33:                        // Hint
        case 0x41: case 0xDC:             // gatilhos
        case 0xE4:                        // Hurt: área de dano
        case 0xE5: case 0xE6: case 0xE9:  // nós de IA
        case 0xE8: case 0xEA: case 0xEB: case 0xEC: case 0xED: case 0xEE: case 0xEF:
        case 0xF4:                        // MoviePlayer
        case 0xF9: case 0xFB:             // gatilhos de som e música
        case 0xFD:                        // CamSubject
            return ObjectKind::Volume;
        default:
            return ObjectKind::Visible;
    }
}

LevelScene::LevelScene(const FilesBin& files, const std::string& level) : level_(level) {
    const Bytes pak = files.load(level + ".bin");
    const auto entries = parse_pak(pak);

    MapItem* main = nullptr;
    for (const PakItem& e : entries) {
        if (!e.is_map()) continue;
        ByteView v = e.view(pak);
        items_.push_back(std::make_unique<MapItem>(e.name, int(e.type), Bytes(v.begin(), v.end())));
        if (e.type == ItemType::MapMain && !main) main = items_.back().get();
    }
    if (!main) throw FormatError("o pacote " + level + " não tem mapa principal");

    // Entidades com hashcode vão para a tabela global portada do jogo (engine/hashtable), como faz
    // parsemap_block_entity_params; as entradas de posicionamento as resolvem por hashtable_getitem.
    hashtable_initialise();
    size_t total = 0;
    for (const auto& item : items_) total += item->entity_count();
    entity_refs_.reserve(total);  // os ponteiros guardados na tabela precisam continuar válidos
    for (const auto& item : items_) {
        for (size_t i = 0; i < item->entity_count(); ++i) {
            const uint32_t h = item->entity_hash(i);
            if (h == 0xFFFFFFFF || hashtable_getitem(h)) continue;  // o primeiro registro vale
            entity_refs_.push_back({item.get(), i});
            hashtable_additem(h, &entity_refs_.back());
        }
    }
    hashtable_create_sections();

    for (const Placement& pl : read_placements(main->data(), main->blocks())) {
        ++stats_.placements;
        const MapItem* item = nullptr;
        size_t entity = 0;
        if (pl.hash != 0xFFFFFFFF) {
            const auto* ref = static_cast<const EntityRef*>(hashtable_getitem(pl.hash));
            if (!ref) {
                ++stats_.unresolved;
                continue;
            }
            item = ref->item;
            entity = ref->entity;
        } else if (pl.entity < main->entity_count()) {
            item = main;
            entity = pl.entity;
        } else {
            ++stats_.unresolved;
            continue;
        }
        const int mesh = mesh_for(*item, entity);
        if (mesh < 0) {
            ++stats_.without_geometry;
            continue;
        }
        const ObjectKind kind = classify_object(pl.flags);
        instances_.push_back({mesh, placement_matrix(pl), pl.flags, kind});
        ++stats_.placed;
        if (kind == ObjectKind::Volume) {
            ++stats_.volumes;
        } else {
            stats_.triangles += meshes_[size_t(mesh)].mesh.indices.size() / 3;
        }
    }
}

int LevelScene::mesh_for(const MapItem& item, size_t entity) {
    for (const auto& [key, idx] : mesh_cache_) {
        if (key.first == &item && key.second == entity) return idx;
    }
    const auto frames_of = [&](int tex) {
        auto it = item.textures().find(tex);
        return it == item.textures().end() ? 1 : it->second.frames;
    };
    EntityMesh mesh = decode_gfx(item.entity_gfx(entity).whole(item.data()), frames_of);
    int result = -1;
    // Este leitor só monta o cenário posicionado (bloco 0x1A); modelos com skinning ficam de fora.
    if (mesh.skinned) ++stats_.skinned_skipped;
    if (!mesh.empty() && !mesh.skinned) {
        for (SubMesh& sm : mesh.submeshes) {
            if (sm.texture < 0) continue;
            const auto found = item.textures().find(sm.texture);
            if (found == item.textures().end()) {
                sm.texture = -1;
                continue;
            }
            int global = -1;
            for (const auto& [key, idx] : texture_cache_) {
                if (key.first == &item && key.second == sm.texture) global = idx;
            }
            if (global < 0) {
                global = int(textures_.size());
                textures_.push_back({&found->second});
                texture_cache_.push_back({{&item, sm.texture}, global});
            }
            sm.texture = global;
        }
        SceneMesh sm{std::move(mesh)};
        for (int k = 0; k < 3; ++k) {
            sm.bmin[k] = 1e30f;
            sm.bmax[k] = -1e30f;
        }
        for (const Vertex& v : sm.mesh.vertices) {
            for (int k = 0; k < 3; ++k) {
                sm.bmin[k] = std::min(sm.bmin[k], v.pos[k]);
                sm.bmax[k] = std::max(sm.bmax[k], v.pos[k]);
            }
        }
        meshes_.push_back(std::move(sm));
        result = int(meshes_.size()) - 1;
    }
    mesh_cache_.push_back({{&item, entity}, result});
    return result;
}

WorldBox transform_box(const float bmin[3], const float bmax[3], const std::array<float, 16>& world) {
    WorldBox b{};
    for (int k = 0; k < 3; ++k) {
        b.lo[k] = 1e30f;
        b.hi[k] = -1e30f;
    }
    for (int corner = 0; corner < 8; ++corner) {
        const float p[3] = {(corner & 1) ? bmax[0] : bmin[0], (corner & 2) ? bmax[1] : bmin[1],
                            (corner & 4) ? bmax[2] : bmin[2]};
        float w[3];
        transform_point(world, p, w);
        for (int k = 0; k < 3; ++k) {
            b.lo[k] = std::min(b.lo[k], w[k]);
            b.hi[k] = std::max(b.hi[k], w[k]);
        }
    }
    return b;
}

void LevelScene::suggested_view(float eye[3], float target[3]) const {
    std::vector<WorldBox> boxes;
    for (const SceneInstance& inst : instances_) {
        if (inst.kind != ObjectKind::Visible) continue;
        const SceneMesh& m = meshes_[size_t(inst.mesh)];
        boxes.push_back(transform_box(m.bmin, m.bmax, inst.world));
    }
    suggest_view(boxes, eye, target);
}

void suggest_view(const std::vector<WorldBox>& boxes, float eye[3], float target[3]) {
    struct Info {
        float c[3], lo[3], hi[3], diag;
    };
    std::vector<Info> infos;
    for (const WorldBox& box : boxes) {
        Info in{};
        for (int k = 0; k < 3; ++k) {
            in.lo[k] = box.lo[k];
            in.hi[k] = box.hi[k];
        }
        float d2 = 0;
        for (int k = 0; k < 3; ++k) {
            in.c[k] = 0.5f * (in.lo[k] + in.hi[k]);
            d2 += (in.hi[k] - in.lo[k]) * (in.hi[k] - in.lo[k]);
        }
        in.diag = std::sqrt(d2);
        infos.push_back(in);
    }
    if (infos.empty()) {
        eye[0] = eye[1] = eye[2] = 10.0f;
        target[0] = target[1] = target[2] = 0.0f;
        return;
    }
    float mc[3];
    for (int k = 0; k < 3; ++k) {
        std::vector<float> v;
        for (const Info& in : infos) v.push_back(in.c[k]);
        mc[k] = median(v);
    }
    std::vector<float> diags;
    for (const Info& in : infos) diags.push_back(in.diag);
    std::sort(diags.begin(), diags.end());
    const float p75 = diags[diags.size() * 3 / 4];
    std::vector<float> dists;
    std::vector<const Info*> rest;
    for (const Info& in : infos) {
        const bool contains = mc[0] >= in.lo[0] && mc[0] <= in.hi[0] && mc[1] >= in.lo[1] && mc[1] <= in.hi[1] &&
                              mc[2] >= in.lo[2] && mc[2] <= in.hi[2];
        if (in.diag > p75 * 8 && contains) continue;  // céu
        rest.push_back(&in);
        const float dx = in.c[0] - mc[0], dy = in.c[1] - mc[1], dz = in.c[2] - mc[2];
        dists.push_back(std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    std::vector<float> sorted = dists;
    std::sort(sorted.begin(), sorted.end());
    const float cut = (sorted.empty() ? 1.0f : sorted[sorted.size() * 8 / 10]) * 1.5f;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < rest.size(); ++i) {
        if (dists[i] > cut) continue;
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], rest[i]->lo[k]);
            hi[k] = std::max(hi[k], rest[i]->hi[k]);
        }
    }
    float size2 = 0;
    for (int k = 0; k < 3; ++k) {
        target[k] = 0.5f * (lo[k] + hi[k]);
        size2 += (hi[k] - lo[k]) * (hi[k] - lo[k]);
    }
    const float r = std::sqrt(size2) * 0.45f;
    const float dir[3] = {0.55f, 0.48f, 0.68f};
    for (int k = 0; k < 3; ++k) eye[k] = target[k] + dir[k] * r;
}

std::vector<std::string> list_levels(const FilesBin& files) {
    std::vector<std::string> out;
    for (const FileEntry& e : files.entries()) {
        if (e.name.size() == 12 && e.name.rfind("070", 0) == 0) out.push_back(e.name.substr(0, 8));
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace nf
