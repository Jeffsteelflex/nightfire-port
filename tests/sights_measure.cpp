// A medida das miras das armas na mão. Ver sights_measure.h.

#include "sights_measure.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "data/map_item.h"
#include "engine/anim.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/player.h"
#include "engine/player_ads.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"

namespace sights {
namespace {

bool g_on = false;
// As posições dos vértices de cada modelo decodificado na carga (rígidos e com skinning, na pose de repouso).
std::map<const celglist_tag*, std::vector<std::array<float, 3>>> g_meshes;
std::map<const celglist_tag*, std::vector<nf::SkinnedVertex>> g_skinned;  // só com NF_SIGHTS_DUMP (os braços)
std::set<uint32_t> g_skins_done;
std::string g_dump_dir;
struct Row {
    uint32_t skin;
    std::string weapons;
    AdsSights s;
    float zoom;
    int quality;
    float drop;
    AdsFindStats st;
};
std::map<uint32_t, Row> g_rows;

// As armas que entram na pose de mira moderna: as de fogo da mão (2 a 0x46), sem luneta e sem zoom (as de zoom sem
// luneta têm uma luneta no modelo; ficam com a visão do jogo), sem o soco, o que se arremessa ou se planta (granadas,
// cargas, minas, o Q-Worm, o relógio) e os lança-foguetes 0x2c-0x31 (o tubo no ombro: a pose de mira olharia para
// dentro dele).
bool ads_candidate(int w, const weapon_definition_tag* d) {
    if (w < 2 || w > 0x46) return false;
    if ((d->flags & 0x40) != 0 || d->zoom_max > 1.0f) return false;  // luneta ou zoom: a visão do jogo
    if ((d->flags & 0x20000) != 0) return false;  // lança um objeto que volta a ser munição
    if (w >= 0x34 && w <= 0x41) return false;      // granadas, o relógio, cargas e minas, o Q-Worm
    if (w >= 0x2c && w <= 0x31) return false;      // lança-foguetes
    return true;
}

// O zoom leve da classe: pistolas 1,25; espingarda e lançadores 1,2; o resto (submetralhadoras, fuzis) 1,35.
float class_zoom(int w) {
    if (w >= 2 && w <= 9) return 1.25f;
    if (w == 0x46 || w == 0x42) return 1.25f;
    if (w == 0x1c || w == 0x1d) return 1.2f;
    if (w >= 0x2a && w <= 0x31) return 1.2f;
    return 1.35f;
}

}  // namespace

void enable() {
    g_on = true;
    if (const char* d = std::getenv("NF_SIGHTS_DUMP")) g_dump_dir = d;
}
bool enabled() { return g_on; }

void note_mesh(const celglist_tag* glist, const nf::EntityMesh& mesh) {
    if (!g_on) return;
    std::vector<std::array<float, 3>> p;
    p.reserve(mesh.vertices.size() + mesh.skin_vertices.size());
    for (const nf::Vertex& v : mesh.vertices) p.push_back({v.pos[0], v.pos[1], v.pos[2]});
    for (const nf::SkinnedVertex& v : mesh.skin_vertices) p.push_back({v.pos[0], v.pos[1], v.pos[2]});
    g_meshes[glist] = std::move(p);
    if (!g_dump_dir.empty() && !mesh.skin_vertices.empty()) g_skinned[glist] = mesh.skin_vertices;
}

void begin_level() {
    g_meshes.clear();
    g_skinned.clear();
}

void measure_level(uint32_t pack) {
    obj_tag* player = glb_players[0];
    if (player == nullptr || player->data == nullptr) return;
    BLData* b = static_cast<BLData*>(player->data);
    obj_tag* gun = b->gun;
    if (gun == nullptr) return;
    ANIMOBJ_tag* pa = anim_of(player);
    const int8_t keep = pa->weapon;
    for (int w = 1; w < 0x5f; ++w) {
        weapon_definition_tag* d = weapon_def(w);
        if (d->skin == 0 || hashtable_getitem(d->skin) == nullptr) continue;
        pa->weapon = int8_t(w);
        Player_SetWeaponAnim(player);
        ANIMOBJ_tag* ga = anim_of(gun);
        if (ga == nullptr) continue;
        ANIMSKIN_tag* skin = ga->skin;
        std::printf("arma %02x classe %02x escolhe %d flags %08x zoom %.2f pele %08x/%08x idle %08x pos %.3f,%.3f,%.3f "
                    "fase %08x\n",
                    w, d->klass, d->selectable, d->flags, double(d->zoom_max), d->skin, skin->hash, d->anim_idle,
                    double(d->offset[0][0]), double(d->offset[0][1]), double(d->offset[0][2]), pack);
        if (!ads_candidate(w, d)) continue;
        auto row = g_rows.find(skin->hash);
        if (row != g_rows.end()) {
            char id[8];
            std::snprintf(id, sizeof(id), " %02x", w);
            if (row->second.weapons.find(id) == std::string::npos) row->second.weapons += id;
            continue;
        }
        if (d->anim_idle != 0) AnimScriptAdd(gun, d->anim_idle);
        Mat_Identity(&gun->matrix);
        Vec_Zero(&gun->pos);
        Vec_Zero(&gun->rot);
        AnimObjectUpdate(gun);
        Mat_Identity(&gun->matrix);
        ga->anim_flags &= ~0xcu;
        psiBuildMatrixPalette(gun, anim_state(ga), 0);
        // O cano: o anexo 0 (Player_MuzzleFlash, Draw_MuzzleFlash).
        _VECTOR muzzle{};
        MATRIX mm;
        AnimGetBoneWorldTrans(gun, 0, 0, &muzzle, &mm);
        const float mz[3] = {muzzle.x, muzzle.y, muzzle.z};
        // As partes da arma no corredor do cano (a segunda arma das duas mãos, peças guardadas na origem ou atrás do
        // olho ficam de fora): o centro da parte até 0,15 m de lado, 0,35 m de altura, de 1,6 m atrás a 0,6 m à frente.
        std::vector<std::array<float, 3>> pts;
        FILE* f = nullptr;
        if (!g_dump_dir.empty()) {
            char path[512];
            std::snprintf(path, sizeof(path), "%s/%08x.tsv", g_dump_dir.c_str(), skin->hash);
            f = std::fopen(path, "w");
            if (f != nullptr) {
                std::fprintf(f, "cano\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\n", double(muzzle.x), double(muzzle.y),
                             double(muzzle.z), double(mm.m[2][0]), double(mm.m[2][1]), double(mm.m[2][2]));
            }
        }
        for (uint32_t i = 0; i < skin->n_discrete; ++i) {
            auto it = g_meshes.find(skin->discrete[i]);
            if (it == g_meshes.end()) continue;
            const MATRIX& bm = ga->bones[skin->discrete_bones[i]];
            std::vector<std::array<float, 3>> part;
            float c[3] = {};
            for (const auto& v : it->second) {
                std::array<float, 3> o;
                for (int k = 0; k < 3; ++k) o[k] = v[0] * bm.m[0][k] + v[1] * bm.m[1][k] + v[2] * bm.m[2][k] + bm.m[3][k];
                for (int k = 0; k < 3; ++k) c[k] += o[k];
                part.push_back(o);
            }
            if (part.empty()) continue;
            for (float& k : c) k /= float(part.size());
            const bool in = std::fabs(c[0] - mz[0]) < 0.15f && std::fabs(c[1] - mz[1]) < 0.35f && mz[2] - 1.6f < c[2] &&
                            c[2] < mz[2] + 0.6f;
            if (!in) continue;
            for (const auto& o : part) {
                pts.push_back(o);
                if (f != nullptr) std::fprintf(f, "%u\t%.5f\t%.5f\t%.5f\n", i, double(o[0]), double(o[1]), double(o[2]));
            }
        }
        if (f != nullptr) {
            // Os braços (a malha com skinning, parte 99 no arquivo): para ver por onde eles passam na pose de mira.
            psiBuildMatrixPalette(gun, anim_state(ga), 1);
            for (uint32_t i = 0; i < skin->n_meshes; ++i) {
                auto it = g_skinned.find(skin->meshes[i]);
                if (it == g_skinned.end()) continue;
                for (const nf::SkinnedVertex& v : it->second) {
                    const MATRIX& m0 = ga->palette[v.bones[0]];
                    const MATRIX& m1 = ga->palette[v.bones[1]];
                    float o[3];
                    for (int k = 0; k < 3; ++k) {
                        const float a = v.pos[0] * m0.m[0][k] + v.pos[1] * m0.m[1][k] + v.pos[2] * m0.m[2][k] + m0.m[3][k];
                        const float c = v.pos[0] * m1.m[0][k] + v.pos[1] * m1.m[1][k] + v.pos[2] * m1.m[2][k] + m1.m[3][k];
                        o[k] = a * v.weight + c * (1.0f - v.weight);
                    }
                    std::fprintf(f, "99\t%.5f\t%.5f\t%.5f\t%d\t%d\t%.3f\n", double(o[0]), double(o[1]), double(o[2]),
                                 v.bones[0], v.bones[1], double(v.weight));
                }
            }
            std::fclose(f);
        }
        Row r;
        r.skin = skin->hash;
        char id[8];
        std::snprintf(id, sizeof(id), " %02x", w);
        r.weapons = id;
        r.zoom = class_zoom(w);
        // A mão direita (osso 21 dos braços): a silhueta da linha de mira começa 10 cm atrás dela.
        const float hand_z = ads_arm_skeleton(skin->parents, skin_bone_count(skin)) ? ga->bones[21].m[3][2] : -1e9f;
        const bool sights = ads_find_sights(reinterpret_cast<const float(*)[3]>(pts.data()), pts.size(), mz,
                                            hand_z - 0.1f, &r.s, &r.st);
        std::printf("  mao_z %.3f\n", double(hand_z));
        r.quality = sights ? 1 : 2;
        r.drop = sights ? 0.0f : 0.035f;
        if (pts.empty() || (mz[0] == 0.0f && mz[1] == 0.0f && mz[2] == 0.0f)) continue;  // sem arma ou sem cano
        g_rows[r.skin] = r;
        std::printf("  linha pele %08x vertices %zu faixa %d comprimento %.3f aresta %.3f qualidade %d alca %.4f,%.4f,%.4f "
                    "massa %.4f,%.4f,%.4f\n",
                    r.skin, pts.size(), r.st.band_points, double(r.st.length), double(r.st.span), r.quality,
                    double(r.s.rear[0]), double(r.s.rear[1]), double(r.s.rear[2]), double(r.s.front[0]),
                    double(r.s.front[1]), double(r.s.front[2]));
    }
    pa->weapon = keep;
    Player_SetWeaponAnim(player);
}

void print_table() {
    if (!g_on) return;
    std::printf("\n// tabela das miras (src/engine/player_ads_table.inc)\n");
    for (const auto& [skin, r] : g_rows) {
        std::printf("{0x%08xu, {%.4ff, %.4ff, %.4ff}, {%.4ff, %.4ff, %.4ff}, %.2ff, %d, %.3ff},  // armas%s; faixa %d, "
                    "aresta %.3f de %.3f\n",
                    skin, double(r.s.rear[0]), double(r.s.rear[1]), double(r.s.rear[2]), double(r.s.front[0]),
                    double(r.s.front[1]), double(r.s.front[2]), double(r.zoom), r.quality, double(r.drop),
                    r.weapons.c_str(), r.st.band_points, double(r.st.span), double(r.st.length));
    }
}

}  // namespace sights
