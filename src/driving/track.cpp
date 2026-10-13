// A pista de uma missão de carro (track.h).

#include "driving/track.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>

#include "platform/fmath.h"

namespace drv {

namespace {

// DAT_00381170 (= DAT_003c1fe0 do SetDimensions): os passos das medidas empacotadas, 0,25 e 16 (o bit 30).
const float kDimensionSteps[2] = {0.25f, 16.0f};

}  // namespace

std::string track_viv_name(const std::string& track) {
    const size_t u = track.find_last_of('_');
    std::string base = u == std::string::npos ? track : track.substr(u + 1);
    for (char& c : base) c = char(std::toupper(static_cast<unsigned char>(c)));
    return base + ".VIV";
}

Track::Track() = default;
Track::~Track() = default;

UGroup* Track::map_group() const {
    UGroup* root = model_.carp_root();
    if (!root) return nullptr;
    UGroup* map = UGroup_GroupLocateTag(root, 0x4d617020);  // 'Map '
    return map == UGroup_GroupEnd(root) ? nullptr : map;
}

bool Track::load(const nf::DiscSource& disc, const std::string& track, const std::string& viv, std::string& error) {
    name_ = track;
    viv_ = viv.empty() ? track_viv_name(track) : viv;
    model_.set_resolve_carp(true);
    // [driving] 0x0022f898 WWorld::LoadTrackFile: data\track\<pista>.crp; as texturas do 'sn' (o <pista>.ssh).
    if (!model_.load_carp(disc, viv_, "data\\track\\" + track + ".crp", {"data\\track\\" + track + ".ssh"}, track, error))
        return false;
    UGroup* map = map_group();
    if (!map) {
        error = "a pista não tem o grupo 'Map '";
        return false;
    }
    // [driving] 0x0021eae8 WRender::WRender: as instâncias ('in  ', a primeira da família) e as passadas.
    UData* in = UGroup_DataLocateFirst(map, 0x696e2020, 0, -1);
    if (in == UGroup_DataEnd(map)) {
        error = "a pista não tem instâncias ('in  ')";
        return false;
    }
    const CarpInstanceRec* rec = reinterpret_cast<const CarpInstanceRec*>(UData_Data(in));
    const uint32_t count = in->count;
    std::map<uint32_t, int> article_index;
    instances_.resize(count);
    int pass = 0;  // o WRender::WRender guarda a passada da instância anterior quando uma não tem artigo
    for (uint32_t k = count; k-- > 0;) {  // o original percorre de trás para a frente
        const CarpInstanceRec& r = rec[k];
        TrackInstance& t = instances_[k];
        const float* rows[4] = {r.axis_x, r.axis_y, r.axis_z, r.pos};
        for (int row = 0; row < 4; ++row) {
            for (int c = 0; c < 3; ++c) t.matrix[row * 4 + c] = rows[row][c];
            t.matrix[row * 4 + 3] = row == 3 ? 1.0f : 0.0f;
        }
        t.flags = r.flags;
        t.packed_dims = r.packed_dims;
        t.radius = float(r.packed_dims & 0x3ff) * kDimensionSteps[(r.packed_dims >> 30) & 1];
        if (r.article) {
            pass = (r.flags & 4) ? 4 : nf::p32_ptr<const CarpArticle>(r.article)->draw_pass;
            auto it = article_index.find(r.article);
            if (it == article_index.end()) {
                const CarpArticle* a = nf::p32_ptr<const CarpArticle>(r.article);
                TrackArticle ta;
                ta.address = r.article;
                ta.draw_pass = a->draw_pass;
                ta.render_type = a->render_type;
                ta.render_flags = a->render_flags;
                for (int l = 0; l < a->lod_count; ++l) {
                    TrackArticle::Lod lod;
                    lod.distance = a->lods[l].distance;
                    lod.part = a->lods[l].model ? model_.part_of_model(a->lods[l].model) : -1;
                    if (lod.part < 0) ++stats_.lods_without_model;
                    ta.lods.push_back(lod);
                    ++stats_.lods;
                }
                it = article_index.emplace(r.article, int(articles_.size())).first;
                articles_.push_back(ta);
            }
            t.article = it->second;
        } else {
            ++stats_.instances_without_article;
        }
        t.pass = std::min(pass, 4);
        ++stats_.pass_count[t.pass];
    }
    // Os nomes dos artigos (o 'Name' do grupo 'Arti' que contém o registro): para os relatórios.
    UGroup* root = model_.carp_root();
    UData* groups = UGroup_GetArray(root);
    for (uint32_t g = 0; g < UGroup_GroupCount(root); ++g) {
        if (UData_MatchTag(groups + g) != 0x41727469) continue;  // 'Arti'
        UData* name = UGroup_DataLocateTag(groups + g, 0x4e616d65);
        if (name == UGroup_DataEnd(groups + g)) continue;
        UData* d = UGroup_GroupEnd(groups + g);
        for (uint32_t i = 0; i < groups[g].count; ++i) {
            auto it = article_index.find(nf::p32_u(UData_Data(d + i)));
            if (it != article_index.end()) articles_[size_t(it->second)].name = reinterpret_cast<const char*>(UData_Data(name));
        }
    }
    stats_.instances = int(instances_.size());
    stats_.articles = int(articles_.size());
    return true;
}

// [driving] 0x001ad128 GetInstanceViewDistance (pelo 0x001ad0b8 GetArticleViewDistance)
float Track::view_distance(const TrackInstance& inst, const float eye[3], float lod_multiplier) {
    const float dx = eye[0] - inst.matrix[12], dy = eye[1] - inst.matrix[13], dz = eye[2] - inst.matrix[14];
    float d = std::sqrt(dx * dx + dy * dy + dz * dz) - inst.radius;
    if (d < 0.0f) d = 0.0f;
    return d * lod_multiplier;
}

// [driving] 0x001ad2e0 DrawGroupDrawModel (a escolha do LOD)
int Track::lod_part(const TrackInstance& inst, float distance) const {
    if (inst.article < 0) return -1;
    const TrackArticle& a = articles_[size_t(inst.article)];
    size_t lod = 0;
    if (!(inst.flags & 8)) {
        while (lod < a.lods.size() && distance > a.lods[lod].distance) ++lod;
    }
    if (lod >= a.lods.size()) return -1;
    return a.lods[lod].part;
}

void Track::visible(const float m[16], const float eye[3], std::vector<Draw>& out, float lod_multiplier) const {
    out.clear();
    // Os planos do volume de visão a partir das colunas de p x M (Gribb-Hartmann): -w <= x <= w, -w <= y <= w, 0 <= z <= w.
    auto col = [&](int c, float v[4]) {
        for (int r = 0; r < 4; ++r) v[r] = m[r * 4 + c];
    };
    float cx[4], cy[4], cz[4], cw[4];
    col(0, cx);
    col(1, cy);
    col(2, cz);
    col(3, cw);
    float planes[6][4];
    for (int i = 0; i < 4; ++i) {
        planes[0][i] = cw[i] + cx[i];
        planes[1][i] = cw[i] - cx[i];
        planes[2][i] = cw[i] + cy[i];
        planes[3][i] = cw[i] - cy[i];
        planes[4][i] = cz[i];
        planes[5][i] = cw[i] - cz[i];
    }
    for (auto& p : planes) {
        const float l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (l > 0)
            for (float& v : p) v /= l;
    }
    const std::vector<CarPart>& parts = model_.parts();
    for (size_t i = 0; i < instances_.size(); ++i) {
        const TrackInstance& inst = instances_[i];
        if (inst.article < 0) continue;
        const float dist = view_distance(inst, eye);
        const int part = lod_part(inst, dist * lod_multiplier);
        if (part < 0) continue;
        if (inst.pass != 4) {
            const CarPart& p = parts[size_t(part)];
            const float c[3] = {0.5f * (p.bbox_min[0] + p.bbox_max[0]), 0.5f * (p.bbox_min[1] + p.bbox_max[1]),
                                0.5f * (p.bbox_min[2] + p.bbox_max[2])};
            float w[3];
            for (int k = 0; k < 3; ++k)
                w[k] = c[0] * inst.matrix[k] + c[1] * inst.matrix[4 + k] + c[2] * inst.matrix[8 + k] + inst.matrix[12 + k];
            float scale = 0;
            for (int r = 0; r < 3; ++r)
                scale = std::max(scale, std::sqrt(inst.matrix[r * 4] * inst.matrix[r * 4] +
                                                  inst.matrix[r * 4 + 1] * inst.matrix[r * 4 + 1] +
                                                  inst.matrix[r * 4 + 2] * inst.matrix[r * 4 + 2]));
            const float dx = p.bbox_max[0] - p.bbox_min[0], dy = p.bbox_max[1] - p.bbox_min[1],
                        dz = p.bbox_max[2] - p.bbox_min[2];
            const float radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz) * scale;
            bool inside = true;
            for (const auto& pl : planes)
                if (pl[0] * w[0] + pl[1] * w[1] + pl[2] * w[2] + pl[3] < -radius) {
                    inside = false;
                    break;
                }
            if (!inside) continue;
        }
        Draw d;
        d.instance = int(i);
        d.part = part;
        d.pass = inst.pass;
        d.distance = dist;
        out.push_back(d);
    }
}

void Track::bounds(float lo[3], float hi[3]) const {
    for (int c = 0; c < 3; ++c) {
        lo[c] = 1e30f;
        hi[c] = -1e30f;
    }
    for (const TrackInstance& t : instances_) {
        if (t.pass == 4 || t.article < 0) continue;
        for (int c = 0; c < 3; ++c) {
            lo[c] = std::min(lo[c], t.matrix[12 + c]);
            hi[c] = std::max(hi[c], t.matrix[12 + c]);
        }
    }
}

}  // namespace drv
