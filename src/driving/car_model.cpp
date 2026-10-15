// Um modelo de carro do EAGL pronto para o PC (car_model.h).

#include "driving/car_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "driving/carp.h"
#include "driving/eagl_model.h"
#include "driving/light_manager.h"
#include "driving/render_method.h"
#include "driving/vif.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"

namespace drv {

using namespace eagl;

namespace {

// Um bloco de bytes alinhado a 16 que não muda de lugar (o carregador grava ponteiros para dentro dele).
struct Blob {
    std::vector<uint8_t> store;
    uint8_t* data = nullptr;
    size_t size = 0;
    void assign(const std::vector<uint8_t>& src) {
        store.assign(src.size() + 16, 0);
        data = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(store.data()) + 15) & ~uintptr_t(15));
        std::memcpy(data, src.data(), src.size());
        size = src.size();
    }
};

struct Vec4 {
    float v[4];
};

}  // namespace

struct CarModel::Impl {
    Bigf misc, mission;
    Blob eaglrm, bondrm, carp, dat, rel;
    Carp carp_dir;
    std::unique_ptr<DynamicLoader> eaglrm_loader, bondrm_loader, model_loader;
    std::vector<RenderMethodInfo> methods;
    // As texturas registradas como "shapes" (nome de 4 letras -> dados da imagem do TAR).
    std::map<std::string, TarImage*> shapes;
    // Os símbolos do jogo que o port fornece.
    float matrices[8][16];  // gpModelViewProjectionMatrix, gpModelMatrix, gpModelViewMatrix...
    Vec4 lighting[7];                    // os tipos 0-5 e o ReflectionInfo2
    Vec4 reflection_info1;
    Vec4 switch_data[16];
    Vec4 damage_zones[16];
    uint32_t shadow_colour[4] = {0x80303030, 0, 0, 0};
    uint32_t specular_tar = 0, reflection_tar = 0;
    // Os símbolos do mundo (RCARPFile::RegisterSymbols 0x001a8930): o tempo das texturas animadas (RTimeData), a
    // posição da câmera (RRenderer + 0x40) e o ReflStretch (0x3336d0), que o microprograma do Reflect não lê; o
    // EnvironmentMap é o 'bref' (o TAR com o TFX em MODULATE).
    Vec4 sim_step{}, camera_pos{}, refl_stretch{};
    Vec4 light_sources[16]{};  // GAME::LightSources: *(RLightManager + 0x7c) (o bloco das luzes, MatDynamicTexture)
    uint32_t environment_tar = 0;
    // Os personagens (docs/driving.md seção 20): o EAGLAnimationBuffer (o ActSkeleton::fSkinnedTransforms: uma matriz por
    // osso, a identidade na carga), o GAME::ActorLightSources (o bloco "Character" do RLightManager, +0xf0), o
    // GAME::PositionalLights (*(RLightManager + 0x2a4): as luzes pontuais, apagadas), o GAME::CharacterOptions (o
    // ActManager + 0x40 que o ActCharacter::Draw 0x0010d320 preenche: alfa / 2, escuridão da sombra x 128, sombra 0/1,
    // o alfa do vértice) e as propriedades de luz da entrada "Characters" do RReflection.
    Vec4 anim_buffer[kCarAnimBufferBones * 4]{};
    Vec4 actor_lights[7]{};
    Vec4 positional_lights[8]{};
    uint32_t character_options[4] = {127, 19, 0, 127};
    Vec4 char_lighting[7]{};
    Vec4 specular_mat[4]{};
    std::vector<std::string>* messages = nullptr;
};

void CarModel::bounds(unsigned view, float lo[3], float hi[3]) const {
    for (int c = 0; c < 3; ++c) {
        lo[c] = 1e30f;
        hi[c] = -1e30f;
    }
    auto grow = [&](const CarPart& p, const float* m) {
        if (!p.vertex_count && !p.bond_vertex_count) return;
        for (int k = 0; k < 8; ++k) {
            const float v[3] = {(k & 1) ? p.bbox_max[0] : p.bbox_min[0], (k & 2) ? p.bbox_max[1] : p.bbox_min[1],
                                (k & 4) ? p.bbox_max[2] : p.bbox_min[2]};
            for (int c = 0; c < 3; ++c) {
                const float t = m ? v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c] + m[12 + c] : v[c];
                lo[c] = std::min(lo[c], t);
                hi[c] = std::max(hi[c], t);
            }
        }
    };
    if (instances_.empty()) {
        for (const CarPart& p : parts_) grow(p, nullptr);
    } else {
        std::vector<uint16_t> list = view ? view_draw_list(view) : std::vector<uint16_t>();
        if (list.empty())
            for (size_t i = 0; i < instances_.size(); ++i) list.push_back(uint16_t(i));
        for (uint16_t i : list)
            if (i < instances_.size() && instances_[i].part >= 0)
                grow(parts_[size_t(instances_[i].part)], instances_[i].matrix);
    }
    if (lo[0] > hi[0])
        for (int c = 0; c < 3; ++c) lo[c] = hi[c] = 0.0f;
}

void CarModel::set_world_inputs(const float light_sources[7][4], const float sim_step[4]) {
    for (int r = 0; r < 7; ++r) std::memcpy(impl_->light_sources[r].v, light_sources[r], 16);
    std::memcpy(impl_->sim_step.v, sim_step, 16);
}

UGroup* CarModel::carp_root() const { return impl_->carp.data ? reinterpret_cast<UGroup*>(impl_->carp.data) : nullptr; }
const uint8_t* CarModel::carp_data() const { return impl_->carp.data; }
size_t CarModel::carp_size() const { return impl_->carp.size; }
const Carp& CarModel::carp_dir() const { return impl_->carp_dir; }

bool CarModel::vehicle_info(VehicleModelInfo& out) const {
    const CarpGroup* arti = impl_->carp_dir.group(carp_tag("Arti"));
    if (!arti || !vehicle_model_info(impl_->carp.data, impl_->carp.size, *arti, out)) return false;
    // Os 'as' em ordem (UGroup::DataLocateFirst 'as  '): o byte +0x1e é o tipo (3 a sombra, 4 o volume), a palavra 9 a
    // etiqueta 'sr' com o nome do __Model.
    for (const CarpEntry& e : arti->data) {
        if (!e.short_tag() || (e.tag >> 16) != 0x6173 || e.size() < 40 || e.data + 40 > impl_->carp.size) continue;
        const uint8_t kind = impl_->carp.data[e.data + 0x1e];
        if (kind != 3 && kind != 4) continue;
        uint32_t sr_tag;
        std::memcpy(&sr_tag, impl_->carp.data + e.data + 36, 4);
        const CarpEntry* sr = arti->find(sr_tag);
        if (!sr) continue;
        std::string name = carp_string(impl_->carp.data, impl_->carp.size, *sr);
        if (name.rfind("EAGL::", 0) == 0) name.erase(0, 6);
        for (size_t k = 0; k < parts_.size(); ++k)
            if (parts_[k].name == name) (kind == 3 ? out.shadow_part : out.shadow_volume_part) = int32_t(k);
    }
    return true;
}

int CarModel::part_of_model(uint32_t model) const {
    auto it = part_by_model_.find(model);
    return it == part_by_model_.end() ? -1 : it->second;
}

// [driving] 0x001c8ba8 RSceneObj::GetViewDrawList
std::vector<uint16_t> CarModel::view_draw_list(unsigned view) const {
    // O jogo procura 'vd' | vista no grupo do objeto (0xffffffff: 'vd  ') e devolve o vetor de u16 e a contagem.
    return view < view_lists_.size() ? view_lists_[view] : std::vector<uint16_t>();
}

void car_lighting_from_tuning(const std::string& text, const std::string& car, float out[7][4]) {
    // Os padrões do RReflection (0x001c0930): Ambient 0,5, Diffuse 0,5, Specular 1,0, Reflect 0,8 em cada tipo;
    // Fresnel 0,21, ReflectionWarp 0,37, SpecularSpot 1,5, 1.
    for (int t = 0; t < 6; ++t) {
        out[t][0] = 0.5f;
        out[t][1] = 0.5f;
        out[t][2] = 1.0f;
        out[t][3] = 0.8f;
    }
    out[6][0] = 0.21f;
    out[6][1] = 0.37f;
    out[6][2] = 1.5f;
    out[6][3] = 1.0f;
    if (car == "Characters") {
        // O RReflection (0x001c0930) põe 1,0 no ambiente e na difusa dos tipos 2 e 3 da entrada "Characters" (+0x20,
        // +0x24, +0x30, +0x34) antes de ler o .tun.
        out[2][0] = out[2][1] = out[3][0] = out[3][1] = 1.0f;
    }
    struct Key {
        const char* name;
        int type, comp;
    };
    static const Key keys[] = {
        {"Fresnel", 6, 0},          {"ReflectionWarp", 6, 1},     {"SpecularSpot", 6, 2},
        {"[Paint]Diffuse", 2, 1},   {"[Paint]Ambient", 2, 0},     {"[Paint]Reflect", 2, 3},
        {"[Paint]Specular", 2, 2},  {"[Dull]Diffuse", 3, 1},      {"[Dull]Ambient", 3, 0},
        {"[SpecBump]Diffuse", 1, 1}, {"[SpecBump]Ambient", 1, 0}, {"[Glass]Diffuse", 0, 1},
        {"[Glass]Ambient", 0, 0},   {"[Glass]Reflect", 0, 3},     {"[Glass]Specular", 0, 2},
        {"[Chrome]Diffuse", 4, 1},  {"[Chrome]Ambient", 4, 0},    {"[Chrome]Reflect", 4, 3},
        {"[Chrome]Specular", 4, 2}, {"[Dash]Diffuse", 5, 1},      {"[Dash]Ambient", 5, 0},
        {"[Dash]Reflect", 5, 3},    {"[Dash]Specular", 5, 2},
    };
    // Duas passadas: "{default}" e depois "{carro}".
    for (const std::string& who : {std::string("default"), car}) {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            // As linhas dos .tun terminam em "\r\n\r": o "\r" do fim vai para o começo da linha seguinte.
            while (!line.empty() && (line[0] == '\r' || line[0] == ' ' || line[0] == '\t')) line.erase(0, 1);
            const size_t eq = line.find('=');
            const size_t ob = line.find('{');
            const size_t cb = line.find('}');
            if (eq == std::string::npos || ob == std::string::npos || cb == std::string::npos || cb < ob) continue;
            if (line.substr(ob + 1, cb - ob - 1) != who) continue;
            const std::string key = line.substr(0, ob);
            const float v = std::strtof(line.c_str() + eq + 1, nullptr);
            for (const Key& k : keys)
                if (key == k.name) out[k.type][k.comp] = v;
        }
    }
}

void car_lights_from_tuning(const std::string& text, CarConstants& k) {
    auto vec = [&](const std::string& key, float* out, int n) {
        const size_t p = text.find(key + "=");
        if (p == std::string::npos) return false;
        const char* c = text.c_str() + p + key.size() + 1;
        for (int i = 0; i < n; ++i) {
            char* end = nullptr;
            out[i] = std::strtof(c, &end);
            c = end;
            if (*c == ',') ++c;
        }
        return true;
    };
    float col[2][4] = {}, amb[4] = {}, ay[2] = {}, ax[2] = {};
    vec("Light1{World}", col[0], 4);
    vec("Light2{World}", col[1], 4);
    vec("AmbientSky{World}", amb, 4);
    vec("Light1AngleY{World}", &ay[0], 1);
    vec("Light1AngleX{World}", &ax[0], 1);
    vec("Light2AngleY{World}", &ay[1], 1);
    vec("Light2AngleX{World}", &ax[1], 1);
    std::memset(k.light_dir_t, 0, sizeof k.light_dir_t);
    std::memset(k.light_col, 0, sizeof k.light_col);
    for (int i = 0; i < 2; ++i) {
        const float y = ay[i] * 3.14159265f / 180.0f, x = ax[i] * 3.14159265f / 180.0f;
        const float d[3] = {-nf::fm::cos(x) * nf::fm::sin(y), -nf::fm::sin(x), -nf::fm::cos(x) * nf::fm::cos(y)};
        for (int c = 0; c < 3; ++c) {
            k.light_dir_t[c][i] = d[c];
            k.light_col[i][c] = col[i][c];
        }
    }
    for (int c = 0; c < 3; ++c) k.ambient[c] = amb[c];
}

CarModel::CarModel() : impl_(std::make_unique<Impl>()) {}
CarModel::~CarModel() {
    // Os objetos do EAGL saem da lista global antes dos buffers que eles apontam; a lista de microprogramas residentes
    // (que escreve no objeto de cada microprograma) também.
    GeoPrim_ResetMicrocode();
    impl_->model_loader.reset();
    impl_->bondrm_loader.reset();
    impl_->eaglrm_loader.reset();
}

bool CarModel::load(const std::string& root, const std::string& viv, const std::string& name, std::string& error) {
    std::string open_error;
    const std::shared_ptr<nf::DiscSource> disc = nf::open_disc(std::wstring(root.begin(), root.end()), &open_error);
    if (disc == nullptr) {
        error = "não abri os dados em " + root + ": " + open_error;
        return false;
    }
    return load(*disc, viv, name, error);
}

bool CarModel::load(const nf::DiscSource& disc, const std::string& viv, const std::string& name, std::string& error) {
    return load_carp(disc, viv, "data\\car\\model\\" + name + ".crp", {"data\\car\\model\\" + name + ".ssh"}, name, error);
}

bool CarModel::load_eagl(const nf::DiscSource& disc, const std::string& viv, const std::string& dat_key,
                         const std::string& rel_key, const std::vector<std::string>& ssh_keys, const std::string& name,
                         std::string& error) {
    split_dat_ = dat_key;
    split_rel_ = rel_key;
    const bool ok = load_carp(disc, viv, std::string(), ssh_keys, name, error);
    split_dat_.clear();
    split_rel_.clear();
    return ok;
}

bool CarModel::load_carp(const nf::DiscSource& disc, const std::string& viv, const std::string& crp_key,
                         const std::vector<std::string>& ssh_keys, const std::string& name, std::string& error) {
    Impl& I = *impl_;
    I.messages = &messages_;
    reset_registries();
    diagnostics().clear();
    RStateManager_Reset();
    tar_images_reset();
    GeoPrim_ResetMicrocode();
    register_eagl_types();
    runtime_alloc_unported = 0;

    const std::string dir = "DRIVING/";
    if (!I.misc.open(disc, dir + "MISC.VIV")) {
        error = "não abri " + dir + "MISC.VIV em " + disc.describe();
        return false;
    }
    if (!I.mission.open(disc, dir + viv)) {
        error = "não abri " + dir + viv + " em " + disc.describe();
        return false;
    }

    // As matrizes do EAGL::ViewPort (RegisterVar, 0x002a0088): o port as deixa na identidade; o renderizador põe a
    // câmera nos shaders.
    static const char* kMatrices[] = {"EAGL::ViewPort::gpModelViewProjectionMatrix", "EAGL::ViewPort::gpModelMatrix",
                                      "EAGL::ViewPort::gpModelViewMatrix",           "EAGL::ViewPort::gpViewMatrix",
                                      "EAGL::ViewPort::gpProjectionMatrix",          "EAGL::ViewPort::gpViewProjectionMatrix"};
    for (int m = 0; m < 6; ++m) {
        std::memset(I.matrices[m], 0, sizeof I.matrices[m]);
        for (int d = 0; d < 4; ++d) I.matrices[m][d * 5] = 1.0f;
        register_var(kMatrices[m], addr_of(I.matrices[m]));
    }

    // As texturas: o .ssh do carro, o common.ssh (o 'carS' do especular) e o ext.ssh (o reflexo estático).
    auto load_ssh = [&](Bigf& arc, const std::string& key) {
        const int idx = arc.find(key);
        if (idx < 0) {
            messages_.push_back("sem " + key);
            return;
        }
        std::vector<uint8_t> bytes;
        arc.read(idx, bytes);
        ShpsFile f;
        if (!shps_parse(bytes.data(), bytes.size(), f)) {
            messages_.push_back(key + ": não é SHPS");
            return;
        }
        for (const std::string& w : f.warnings) messages_.push_back(key + ": " + w);
        for (ShpsImage& im : f.images) {
            if (im.rgba.empty() || I.shapes.count(im.name)) continue;
            CarTexture t;
            t.name = im.name;
            t.source = key;
            t.width = im.width;
            t.height = im.height;
            t.rgba = std::move(im.rgba);
            const uint16_t id = uint16_t(textures_.size());
            I.shapes[t.name] = tar_image_create(id, t.width, t.height, im.gs_psm, im.palette_type != 0);
            textures_.push_back(std::move(t));
        }
    };
    // As texturas do personagem no lugar das do Actors.ssh ('hhhh' a cabeça, 'tttt' o tronco, 'bbbb' o corpo): o que
    // o ActCharacter::LoadCharacter (ActTextureDatabase::LoadTexture) e o ActModel::SetTexture fazem no desenho.
    for (const auto& al : texture_aliases_) {
        const int idx = I.mission.find(al.second);
        std::vector<uint8_t> bytes;
        ShpsFile f;
        if (idx < 0 || !I.mission.read(idx, bytes) || !shps_parse(bytes.data(), bytes.size(), f) || f.images.empty() ||
            f.images[0].rgba.empty() || I.shapes.count(al.first)) {
            messages_.push_back("sem a textura " + al.second + " (" + al.first + ")");
            continue;
        }
        ShpsImage& im = f.images[0];
        CarTexture t;
        t.name = al.first;
        t.source = al.second;
        t.width = im.width;
        t.height = im.height;
        t.rgba = std::move(im.rgba);
        const uint16_t id = uint16_t(textures_.size());
        I.shapes[t.name] = tar_image_create(id, t.width, t.height, im.gs_psm, im.palette_type != 0);
        textures_.push_back(std::move(t));
    }
    for (const std::string& key : ssh_keys) load_ssh(I.mission, key);
    load_ssh(I.mission, "data\\render\\common.ssh");
    load_ssh(I.mission, "data\\render\\ext.ssh");
    set_shape_lookup(
        [](const char n[4], void* user) -> TarImage* {
            auto* impl = static_cast<Impl*>(user);
            auto it = impl->shapes.find(std::string(n, 4));
            return it == impl->shapes.end() ? nullptr : it->second;
        },
        &I);

    // As propriedades de luz do carro (o CarRender default.tun da missão).
    {
        float props[7][4];
        std::string tun;
        const int idx = I.mission.find("data\\tuning\\Render\\CarRender\\default.tun");
        if (idx >= 0) {
            std::vector<uint8_t> b;
            I.mission.read(idx, b);
            tun.assign(b.begin(), b.end());
        } else {
            messages_.push_back("sem o CarRender default.tun: as propriedades padrão do RReflection");
        }
        car_lighting_from_tuning(tun, name, props);
        for (int t = 0; t < 7; ++t) std::memcpy(I.lighting[t].v, props[t], 16);
        {
            char b[160];
            std::snprintf(b, sizeof b, "propriedades %zu bytes de tun: pintura %.2f %.2f %.2f %.2f fosco %.2f %.2f reflexo %.2f %.2f %.2f",
                          tun.size(), props[2][0], props[2][1], props[2][2], props[2][3], props[3][0], props[3][1],
                          props[6][0], props[6][1], props[6][2]);
            messages_.push_back(b);
        }
        std::memcpy(constants_.refl_info2, props[6], 16);
        // Os personagens: a entrada "Characters" do RReflection.
        float cprops[7][4];
        car_lighting_from_tuning(tun, "Characters", cprops);
        for (int t = 0; t < 7; ++t) std::memcpy(I.char_lighting[t].v, cprops[t], 16);
    }
    // Os personagens: o EAGLAnimationBuffer na identidade (a pose de ligação; quem anima troca as matrizes no desenho)
    // e as entradas do quadro que o port ainda não calcula (as luzes pontuais apagadas, a SpecularMat identidade).
    for (int b = 0; b < kCarAnimBufferBones; ++b)
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) I.anim_buffer[b * 4 + r].v[c] = r == c ? 1.0f : 0.0f;
    std::memset(I.positional_lights, 0, sizeof I.positional_lights);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) I.specular_mat[r].v[c] = r == c ? 1.0f : 0.0f;
    {
        const int idx = I.mission.find("data\\tuning\\Render\\Lighting\\");
        RLightManagerLights lm;
        std::string text;
        if (idx >= 0) {
            std::vector<uint8_t> b;
            I.mission.read(idx, b);
            text.assign(b.begin(), b.end());
        }
        RLightManager_Construct(lm, text);
        std::memcpy(I.actor_lights, &lm.blocks[1], sizeof I.actor_lights);  // RLightManager + 0xf0: o "Character"
    }
    {
        const int idx = I.mission.find("data\\tuning\\Render\\Lighting\\");
        if (idx >= 0) {
            std::vector<uint8_t> b;
            I.mission.read(idx, b);
            car_lights_from_tuning(std::string(b.begin(), b.end()), constants_);
        } else {
            messages_.push_back("sem a tabela de luzes da pista: sem luzes");
        }
    }
    std::memset(I.switch_data, 0, sizeof I.switch_data);
    // O x de cada GAME::SwitchData (o VScroll, que o MatCarTextureSwitchPacked não lê) leva 1000 + n na carga: o lote
    // copia a entrada e o desenho guarda o n para trocar a entrada pela do quadro (driving/render/vehicle.h).
    for (int n = 0; n < 16; ++n) I.switch_data[n].v[0] = 1000.0f + float(n);
    std::memset(I.damage_zones, 0, sizeof I.damage_zones);
    std::memset(I.reflection_info1.v, 0, 16);
    I.specular_tar = tar_create("carS", true, true);
    I.reflection_tar = tar_create("bref", true, true);
    I.environment_tar = tar_create("bref", true, true);
    ptr_at<uint32_t>(I.environment_tar)[4] &= 0xF9FFFFFFu;  // o TFX em MODULATE (puVar3[4] & 0xf9ffffff)

    // A função de resolução do dono (o ResolveEAGLReferences do jogo, 0x001a9440, com o que o port fornece).
    Resolver resolver = [&I](const char* sym, bool& found) -> uint32_t {
        found = true;
        static const struct {
            const char* name;
            int type;
        } props[] = {{"GAME::CarLightingGlassProps", 0},  {"GAME::CarLightingSpecularProps", 1},
                     {"GAME::CarLightingGlossyProps", 2}, {"GAME::CarLightingDashProps", 5},
                     {"GAME::CarLightingChromeProps", 4}, {"GAME::CarLightingDullProps", 3}};
        for (const auto& p : props)
            if (std::strcmp(sym, p.name) == 0) return addr_of(I.lighting[p.type].v);
        if (!std::strcmp(sym, "GAME::ReflectionInfo2") || !std::strcmp(sym, "GAME::WorldSpecularInfo"))
            return addr_of(I.lighting[6].v);
        if (!std::strcmp(sym, "GAME::ReflectionInfo1")) return addr_of(I.reflection_info1.v);
        if (!std::strcmp(sym, "GAME::SpecularMap")) return I.specular_tar;
        if (!std::strcmp(sym, "GAME::ReflectionMap")) return I.reflection_tar;
        if (!std::strncmp(sym, "GAME::SwitchData[", 17)) return addr_of(I.switch_data[std::atoi(sym + 17) & 15].v);
        // Sem índice: a entrada 8 (0x4245a0 = 0x424520 + 8 x 16 no RegisterSymbols do PS2).
        if (!std::strcmp(sym, "GAME::SwitchData")) return addr_of(I.switch_data[8].v);
        if (!std::strcmp(sym, "GAME::DamageZones") || !std::strcmp(sym, "EAGL::DamageZones"))
            return addr_of(I.damage_zones[0].v);
        if (!std::strcmp(sym, "EAGL::CarShadowColour")) return addr_of(I.shadow_colour);
        if (!std::strncmp(sym, "STATE::", 7)) return addr_of(RStateManager_GetGeoPrimState(sym + 7));
        if (!std::strcmp(sym, "GAME::SimStep")) return addr_of(I.sim_step.v);
        if (!std::strcmp(sym, "GAME::CameraPos") || !std::strcmp(sym, "EAGL::CameraPos")) return addr_of(I.camera_pos.v);
        if (!std::strcmp(sym, "EAGL::ReflStretch")) return addr_of(I.refl_stretch.v);
        if (!std::strcmp(sym, "GAME::LightSources")) return addr_of(I.light_sources[0].v);
        if (!std::strcmp(sym, "EAGL::EnvironmentMap")) return I.environment_tar;
        // Os personagens (RCARPFile::RegisterSymbols 0x001a8930, ActSkeleton::ActSkeleton 0x00114760,
        // ActManager::SymbolResolverInit 0x00110ee8).
        if (!std::strcmp(sym, "EAGLAnimationBuffer")) return addr_of(I.anim_buffer[0].v);
        if (!std::strcmp(sym, "GAME::ActorLightSources")) return addr_of(I.actor_lights[0].v);
        if (!std::strcmp(sym, "GAME::PositionalLights")) return addr_of(I.positional_lights[0].v);
        if (!std::strcmp(sym, "GAME::CharacterOptions")) return addr_of(I.character_options);
        if (!std::strcmp(sym, "GAME::CharacterLightingDullProps")) return addr_of(I.char_lighting[3].v);
        if (!std::strcmp(sym, "GAME::CharacterLightingGlossyProps")) return addr_of(I.char_lighting[2].v);
        if (!std::strcmp(sym, "GAME::CharacterReflectionInfo")) return addr_of(I.char_lighting[6].v);
        if (!std::strcmp(sym, "GAME::SpecularMat")) return addr_of(I.specular_mat[0].v);
        if (!std::strcmp(sym, "GAME::WeaponReflectionMap")) return I.specular_tar;  // só do port: o 'carS'

        found = false;
        return 0;
    };

    // Os render methods.
    auto load_object = [&](const std::string& key, Blob& blob, std::unique_ptr<DynamicLoader>& loader,
                           const char* label) -> bool {
        const int idx = I.misc.find(key);
        if (idx < 0) {
            error = "sem " + key + " no MISC.VIV";
            return false;
        }
        std::vector<uint8_t> b;
        I.misc.read(idx, b);
        blob.assign(b);
        loader = std::make_unique<DynamicLoader>(blob.data, uint32_t(blob.size), resolver);
        auto census = render_methods_census(*loader, label);
        I.methods.insert(I.methods.end(), census.begin(), census.end());
        return loader->ok();
    };
    if (!load_object("data\\render\\eaglrm.o", I.eaglrm, I.eaglrm_loader, "eaglrm")) return false;
    if (!load_object("data\\render\\bondrm.o", I.bondrm, I.bondrm_loader, "bondrm")) return false;

    // O CARP do carro: 'ELFd' (cabeçalho e .data) e 'ELFr' (o resto), ligados pelo tamanho do 'ELFd' (o RCARPFile,
    // 0x001a9600: DynamicLoader(elf, tamanho, rel, adiar o Resolve, ResolveEAGLReferences) e depois o Resolve).
    // Num objeto partido em arquivos (load_eagl), o .dat e o .rel fazem o mesmo papel, sem o CARP.
    if (!split_dat_.empty()) {
        auto read_any = [&](const std::string& key, Blob& blob) {
            std::vector<uint8_t> b;
            int idx = I.mission.find(key);
            if (idx >= 0) I.mission.read(idx, b);
            else if ((idx = I.misc.find(key)) >= 0) I.misc.read(idx, b);
            if (b.empty()) return false;
            blob.assign(b);
            return true;
        };
        if (!read_any(split_dat_, I.dat) || !read_any(split_rel_, I.rel)) {
            error = "sem " + split_dat_ + " ou " + split_rel_ + " em " + viv;
            return false;
        }
        I.carp = Blob();
        I.carp_dir = Carp();
        I.model_loader = std::make_unique<DynamicLoader>(I.dat.data, uint32_t(I.dat.size), I.rel.data, true, resolver);
        I.model_loader->Resolve();
    } else {
        const int idx = I.mission.find(crp_key);
        if (idx < 0) {
            error = "sem " + crp_key + " em " + viv;
            return false;
        }
        std::vector<uint8_t> b;
        I.mission.read(idx, b);
        I.carp.assign(b);
        Carp& c = I.carp_dir;
        if (!carp_parse(I.carp.data, I.carp.size, c)) {
            error = "o .crp não é CARP";
            return false;
        }
        const CarpEntry* elfd = c.find(carp_tag("ELFd"));
        const CarpEntry* elfr = c.find(carp_tag("ELFr"));
        if (!elfd) {
            error = "o CARP não tem 'ELFd'";
            return false;
        }
        uint8_t* elf = I.carp.data + elfd->data;
        if ((reinterpret_cast<uintptr_t>(elf) & 15) != 0) messages_.push_back("o ELF do CARP não está alinhado a 16");
        I.model_loader = std::make_unique<DynamicLoader>(elf, elfd->size(), elfr ? I.carp.data + elfr->data : nullptr,
                                                         true, resolver);
        if (resolve_carp_) {
            // [driving] 0x001a9600 RCARPFile::Resolve: o arquivo no espaço "CARP", o modelo como o espaço "EAGL"
            // (os símbolos definidos do objeto), CARP::ResolveSymbolicReferences e só então o Resolve do modelo.
            UGroup* root = reinterpret_cast<UGroup*>(I.carp.data);
            UGroup_Deserialize(root);
            CarpSymbols symbols;
            symbols.add_carp_file(root);
            DynamicLoader* loader = I.model_loader.get();
            symbols.add_namespace("EAGL", [loader](const char* name, uint32_t& size) -> uint32_t {
                size = 0;
                return loader->Find(name);
            });
            CARP_ResolveSymbolicReferences(root, symbols);
            carp_misses_ = symbols.misses;
        }
        I.model_loader->Resolve();
    }
    for (const std::string& d : diagnostics()) messages_.push_back("carregador: " + d);
    stats_.loader_messages = int(diagnostics().size());
    stats_.runtime_alloc_unported = runtime_alloc_unported;

    // O desenho: cada modelo (peça), cada grupo visível, cada primitiva; o VIF executa a cadeia e cada MSCNT é um lote.
    Vif1 vif;
    const CarConstants render_constants = constants_;
    if (verify_ && resolve_carp_) {
        // Só para a conferência do mundo: constantes que não deixam as contas triviais (as matrizes identidade da carga
        // deixariam a luz e o mapa de ambiente sem efeito): uma câmera em perspectiva como a MVP, quatro luzes
        // pontuais perto do centro da pista, a névoa e a posição da câmera. O que se desenha não usa estes valores.
        static const float kMvp[16] = {1.2f, 0.0f, 0.1f, 0.0f, 0.0f, 1.6f, 0.2f, 0.0f, 0.3f, -0.1f, 1.0f, 1.0f,
                                       -40.0f, -10.0f, 150.0f, 160.0f};
        std::memcpy(I.matrices[0], kMvp, sizeof kMvp);
        for (int L = 0; L < 4; ++L) {
            constants_.point_pos[0][L] = -300.0f + 160.0f * float(L);
            constants_.point_pos[1][L] = 5.0f + float(L);
            constants_.point_pos[2][L] = -200.0f + 90.0f * float(L);
            constants_.point_pos[3][L] = 0.002f * float(L + 1);
            for (int c = 0; c < 3; ++c) constants_.point_col[L][c] = 0.05f * float(1 + ((L + c) % 3));
        }
        constants_.fog[0] = 5.0f;
        constants_.fog[2] = 600.0f;
        constants_.fog[3] = 0.4f;
        I.camera_pos.v[0] = -350.0f;
        I.camera_pos.v[1] = 12.0f;
        I.camera_pos.v[2] = -400.0f;
    }
    constants_.write(vif.mem);
    const CarMethodLayout* layout = nullptr;
    std::string method_name;
    int part_index = 0;
    std::vector<uint32_t> mc_queue;  // os microprogramas dos pacotes de partida da cadeia corrente, em ordem
    size_t mc_next = 0;
    auto method_of_microcode = [&](uint32_t mc) -> const RenderMethodInfo* {
        for (const RenderMethodInfo& m : I.methods)
            if (m.microcode == mc) return &m;
        return nullptr;
    };
    auto add_batch = [&](const CarBatch& b, bool fallback, const std::string& method) {
        const uint32_t base = uint32_t(vertices_.size());
        const int passes = fallback ? int(kPassBase) : b.layout->passes;
        for (const CarVertexIn& v : b.v) {
            const CarGpuVertex g = car_convert(*b.layout, v);
            vertices_.push_back(g);
        }
        std::vector<uint16_t> tris;
        // O vidro: o vértice que fecha o triângulo vem primeiro (o provocador decide a face de trás no shader).
        const bool glass = !fallback && b.layout->kind == kCarGlass;
        car_triangles(b, tris, glass);
        int tex[kCarMaxPasses] = {-1, -1, -1, -1};
        for (int p = 0; p < kCarMaxPasses; ++p)
            if (b.pass[p].has_tex0) {
                const int id = int(b.pass[p].tex0 & 0x3FFF);
                tex[p] = id < int(textures_.size()) ? id : -1;
            }
        // As luzes: o n do GAME::SwitchData pelo x de 1000 + n da carga (o x volta a zero no desenho).
        float sw[4];
        std::memcpy(sw, b.switch_data, 16);
        int sw_index = -1;
        if (!fallback && b.layout->kind == kCarSwitch) {
            const int n = int(sw[0]) - 1000;
            sw_index = n >= 0 && n < 16 ? n : -1;
            sw[0] = 0.0f;
        }
        CarDraw* d = draws_.empty() ? nullptr : &draws_.back();
        const bool same = d && d->fallback == fallback && d->passes == passes && d->method == method &&
                          d->part == part_index &&
                          std::memcmp(d->props, b.props, 16) == 0 && std::memcmp(d->texture, tex, sizeof tex) == 0 &&
                          d->pass[0].prim == b.pass[0].prim && d->pass[0].alpha == b.pass[0].alpha &&
                          d->pass[0].test == b.pass[0].test && d->pass[1].alpha == b.pass[1].alpha &&
                          d->pass[1].test == b.pass[1].test && std::memcmp(d->switch_data, sw, 16) == 0 &&
                          d->switch_index == sw_index && std::memcmp(d->switch_param, b.switch_param, 16) == 0;
        if (!same) {
            CarDraw nd;
            nd.method = method;
            nd.passes = passes;
            nd.fallback = fallback;
            nd.layout = fallback ? nullptr : b.layout;
            std::memcpy(nd.switch_data, sw, 16);
            std::memcpy(nd.switch_param, b.switch_param, 16);
            nd.switch_index = sw_index;
            nd.part = part_index;
            nd.first_index = uint32_t(indices_.size());
            std::memcpy(nd.props, b.props, 16);
            for (int p = 0; p < kCarMaxPasses; ++p) nd.pass[p] = b.pass[p];
            std::memcpy(nd.texture, tex, sizeof tex);
            draws_.push_back(nd);
            d = &draws_.back();
        }
        for (uint16_t t : tris) indices_.push_back(base + t);
        d->index_count += uint32_t(tris.size());
        ++stats_.batches;
        stats_.vertices += b.count;
        stats_.triangles += int(tris.size() / 3);
        stats_.method_batches[method]++;
        stats_.method_triangles[method] += int(tris.size() / 3);
        if (fallback) {
            ++stats_.fallback_batches;
            stats_.fallback_triangles += int(tris.size() / 3);
        }
        batches_.push_back(b);
    };
    // Substituto para os métodos fora da família: os fluxos pelas UNPACK do pacote de vértices do lote, na ordem dos
    // parâmetros por vértice do método (os que vêm depois do "SplitReg3": "Coordinates", "STQ", "Normals"...; o pacote
    // sobe "Parameters" (o número de vértices), as três "SplitReg" e então esses fluxos, nessa ordem); a textura pelo
    // primeiro pacote de TAR do buffer, a primitiva pelo primeiro GeoPrimState. Os fluxos inteiros (os métodos "Packed")
    // passam pela conversão do passo 1 da família (u16 x I1 - 32, u8 x I2 - 1) e tudo vai em float ao mesmo vertex
    // shader, com a passada base da família (a luz e a névoa). Só para o protótipo mostrar a peça inteira; marcado como
    // substituto nos relatórios.
    // A família MatBond do mundo (mat_bond.h): os vértices depois do passo 1, as duas passadas (a textura e o ambiente).
    const BondMethodLayout* bond_layout = nullptr;
    auto add_bond_batch = [&](const BondBatch& b, const std::string& method) {
        const uint32_t base = uint32_t(bond_vertices_.size());
        for (const BondVertexIn& v : b.v) bond_vertices_.push_back(bond_convert(*b.layout, v));
        std::vector<uint16_t> tris;
        bond_triangles(b, tris);
        int tex[kCarMaxPasses] = {-1, -1, -1, -1};
        for (int p = 0; p < 2; ++p)
            if (b.pass[p].has_tex0) {
                const int id = int(b.pass[p].tex0 & 0x3FFF);
                tex[p] = id < int(textures_.size()) ? id : -1;
            }
        const int passes = b.layout->passes;
        CarDraw* d = draws_.empty() ? nullptr : &draws_.back();
        const bool same = d && d->family == 1 && d->passes == passes && d->method == method && d->part == part_index &&
                          std::memcmp(d->texture, tex, sizeof tex) == 0 && d->pass[0].prim == b.pass[0].prim &&
                          d->pass[0].alpha == b.pass[0].alpha && d->pass[0].test == b.pass[0].test &&
                          d->pass[0].clamp == b.pass[0].clamp && d->pass[1].alpha == b.pass[1].alpha &&
                          d->pass[1].prim == b.pass[1].prim && d->pass[1].clamp == b.pass[1].clamp &&
                          std::memcmp(d->scroll, b.scroll, sizeof b.scroll) == 0 &&
                          std::memcmp(d->frame_anim, b.frame_anim, sizeof b.frame_anim) == 0;
        if (!same) {
            CarDraw nd;
            nd.method = method;
            nd.family = 1;
            nd.passes = passes;
            nd.part = part_index;
            nd.first_index = uint32_t(bond_indices_.size());
            nd.pass[0] = b.pass[0];
            nd.pass[1] = b.pass[1];
            std::memcpy(nd.texture, tex, sizeof tex);
            nd.bond_kind = b.layout->kind;
            std::memcpy(nd.scroll, b.scroll, sizeof nd.scroll);
            std::memcpy(nd.frame_anim, b.frame_anim, sizeof nd.frame_anim);
            draws_.push_back(nd);
            d = &draws_.back();
        }
        for (uint16_t t : tris) bond_indices_.push_back(base + t);
        d->index_count += uint32_t(tris.size());
        ++stats_.batches;
        stats_.vertices += b.count;
        stats_.triangles += int(tris.size() / 3);
        stats_.method_batches[method]++;
        stats_.method_triangles[method] += int(tris.size() / 3);
    };
    const RenderMethodInfo* cur_method = nullptr;
    auto fallback_batch = [&](uint32_t top) {
        CarBatch b;
        static const CarMethodLayout kFallback = {"(substituto)", kPassBase, false, 256, 0, 1, 5, 9, 0, 0, 0, 0, 0,
                                                 0, 0, 0, 0, 0, 0, 0, 0, 0};
        b.layout = &kFallback;
        b.count = vif.mem.i(int(top), 0);
        if (b.count <= 0 || b.count > 256) return;
        // Os nomes dos fluxos por vértice do método.
        std::vector<std::string> streams;
        if (cur_method) {
            bool after = false;
            for (const std::string& p : cur_method->params) {
                if (p == "SplitReg3") {
                    after = true;
                    continue;
                }
                if (after) streams.push_back(p);
            }
        }
        if (streams.empty()) streams = {"Coordinates", "STQ", "Normals"};
        // As UNPACK do pacote: a partir do cabeçalho (V4-32 n=1 no TOP+0), sem as SplitReg (V4-8 n=1).
        std::vector<const Vif1::UnpackRecord*> unp;
        std::vector<int> splits;
        bool in_packet = false;
        for (const Vif1::UnpackRecord& r : vif.unpack_log) {
            const int at = int((r.addr - top) & 1023);
            if (r.cmd == 0x6c && r.num == 1 && at == 0) {
                in_packet = true;
                unp.clear();
                splits.clear();
                continue;
            }
            if (!in_packet) continue;
            if ((r.cmd & 0x6f) == 0x6e && r.num == 1) splits.push_back(at);
            else if (r.num == b.count) unp.push_back(&r);
        }
        const Vif1::UnpackRecord* pos = nullptr;
        const Vif1::UnpackRecord* stq = nullptr;
        const Vif1::UnpackRecord* nrm = nullptr;
        for (size_t i = 0; i < unp.size() && i < streams.size(); ++i) {
            if (streams[i] == "Coordinates") pos = unp[i];
            else if (streams[i] == "STQ") stq = unp[i];
            else if (streams[i] == "Normals") nrm = unp[i];
        }
        if (!pos) return;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) {
                b.mvp[r][c] = vif.mem.f(int(top) + 1 + r, c);
                b.world[r][c] = vif.mem.f(int(top) + 5 + r, c);
            }
        for (int c = 0; c < 4; ++c) b.props[c] = vif.mem.f(int(top) + 9, c);
        for (int q = 0; q < 0x140; ++q) {  // o primeiro TAR e o primeiro GeoPrimState no buffer
            const uint32_t w0 = vif.mem.u(int(top) + q, 0), w1 = vif.mem.u(int(top) + q, 1),
                           w2 = vif.mem.u(int(top) + q, 2);
            if (!b.pass[0].has_tex0 && w0 == 0x8001 && w1 == 0x60000000 && w2 == 0xEEEEEE) {
                uint32_t buf[7 * 4];
                for (int i = 0; i < 28; ++i) buf[i] = vif.mem.u(int(top) + q + i / 4, i % 4);
                gs_read_packet(reinterpret_cast<const uint8_t*>(buf), 7, b.pass[0]);
            }
            if (!b.pass[0].has_prim && w0 == 1 && w1 == 0x10000000) {
                uint32_t buf[8 * 4];
                for (int i = 0; i < 32; ++i) buf[i] = vif.mem.u(int(top) + q + i / 4, i % 4);
                gs_read_packet(reinterpret_cast<const uint8_t*>(buf), 8, b.pass[0]);
            }
        }
        b.strip = !b.pass[0].has_prim || (b.pass[0].prim & 7) != 3;
        for (int i = 0; i < 12; ++i)
            b.split[i] = i / 4 < int(splits.size()) ? int(vif.mem.u(int(top) + splits[size_t(i / 4)], i % 4) & 0xFFFF) : 0;
        static const float kI1 = [] { uint32_t u = 0x3a7ffff7; float f; std::memcpy(&f, &u, 4); return f; }();
        static const float kI2 = [] { uint32_t u = 0x3c008080; float f; std::memcpy(&f, &u, 4); return f; }();
        auto is_float = [](int cmd) { return (cmd & 3) == 0; };  // V*-32
        auto fbits = [](float f) {
            uint32_t u;
            std::memcpy(&u, &f, 4);
            return u;
        };
        b.v.resize(size_t(b.count));
        for (int i = 0; i < b.count; ++i) {
            CarVertexIn& v = b.v[size_t(i)];
            const int pq = int(pos->addr) + i;
            for (int c = 0; c < 3; ++c) v.pos[c] = vif.mem.u(pq, c);
            for (int c = 0; c < 3; ++c) {
                float f = c == 2 ? 1.0f : 0.0f;
                if (stq && c < 2)
                    f = is_float(stq->cmd) ? vif.mem.f(int(stq->addr) + i, c)
                                           : float(int32_t(vif.mem.u(int(stq->addr) + i, c))) * kI1 + -32.0f;
                v.stq[c] = fbits(f);
            }
            for (int c = 0; c < 3; ++c) {
                float f = c == 1 ? 1.0f : 0.0f;
                if (nrm)
                    f = is_float(nrm->cmd) ? vif.mem.f(int(nrm->addr) + i, c)
                                           : float(int32_t(vif.mem.u(int(nrm->addr) + i, c) & 0xFF)) * kI2 + -1.0f;
                v.normal[c] = fbits(f);
            }
            for (int c = 0; c < 4; ++c) v.dmgvec[c] = 0;
            for (int c = 0; c < 4; ++c) v.dmgnrm[c] = 0;
        }
        add_batch(b, true, method_name);
    };
    DrawContext ctx;
    ctx.cmd_base = addr_of(I.model_loader->data());
    ctx.gs_zbuf = 0x0000000A000000ull;  // PSMZ24 sem máscara: o Z é gravado (o valor não importa ao PC)
    // A família de pele (mat_char.h): os ossos da paleta que está na VU (a última operação 0xb cujos dados uma UNPACK
    // levou ao endereço da paleta) e os lotes decodificados para o desenho do PC.
    const CharMethodLayout* char_layout = nullptr;
    const std::vector<DrawStats::PaletteUpload>* palette_uploads = nullptr;
    std::vector<int32_t> palette_bones;
    auto note_palette = [&]() {
        if (!palette_uploads) return;
        for (const Vif1::UnpackRecord& r : vif.unpack_log) {
            if (r.addr != 0x22u || !r.src) continue;
            for (const DrawStats::PaletteUpload& u : *palette_uploads)
                if (static_cast<const void*>(u.data) == static_cast<const void*>(r.src)) palette_bones = u.bones;
        }
    };
    auto add_char_batch = [&](const CharBatch& b, const std::string& method) {
        const uint32_t base = uint32_t(char_vertices_.size());
        for (const CharVertex& v : b.v) {
            CharGpuVertex g{};
            // p x escala (a linha 3 da escala é a translação; o w da posição é 1).
            for (int c = 0; c < 3; ++c) {
                float t = 0.0f;
                for (int k = 0; k < 3; ++k) t += v.pos[k] * ps2::ffloat(ctx.palette_scale[k * 4 + c]);
                g.pos[c] = t + ps2::ffloat(ctx.palette_scale[12 + c]);
            }
            // A normal pelas três primeiras linhas da escala (o passo 2 do microprograma passa a normal pela paleta,
            // escala x osso, sem a translação).
            for (int c = 0; c < 3; ++c) {
                float t = 0.0f;
                for (int k = 0; k < 3; ++k) t += v.normal[k] * ps2::ffloat(ctx.palette_scale[k * 4 + c]);
                g.normal[c] = t;
            }
            for (int c = 0; c < 2; ++c) g.st[c] = v.st[c];
            for (int c = 0; c < 3; ++c) {
                g.bone[c] = v.bone[c];
                g.weight[c] = v.weight[c];
                if (v.bone[c] >= 0) char_bones_ = std::max(char_bones_, v.bone[c] + 1);
            }
            g.bone[3] = -1;
            char_vertices_.push_back(g);
        }
        std::vector<uint16_t> tris;
        char_triangles(b, tris);
        int tex[kCarMaxPasses] = {-1, -1, -1, -1};
        for (int p = 0; p < kCharMaxPasses; ++p)
            if (b.pass[p].has_tex0) {
                const int id = int(b.pass[p].tex0 & 0x3FFF);
                tex[p] = id < int(textures_.size()) ? id : -1;
            }
        const bool shadow = (b.options[2] & 0xFFFF) != 0;
        const int passes = shadow ? int(kCharPassBase) : b.layout->passes;
        CarDraw* d = draws_.empty() ? nullptr : &draws_.back();
        const bool same = d && d->family == 2 && d->passes == passes && d->method == method && d->part == part_index &&
                          std::memcmp(d->texture, tex, sizeof tex) == 0 && d->pass[0].prim == b.pass[0].prim &&
                          d->pass[0].alpha == b.pass[0].alpha && d->pass[0].test == b.pass[0].test &&
                          std::memcmp(d->props, b.props, 16) == 0 && std::memcmp(d->char_options, b.options, 16) == 0;
        if (!same) {
            CarDraw nd;
            nd.method = method;
            nd.family = 2;
            nd.passes = passes;
            nd.part = part_index;
            nd.first_index = uint32_t(char_indices_.size());
            for (int p = 0; p < kCharMaxPasses; ++p) nd.pass[p] = b.pass[p];
            std::memcpy(nd.texture, tex, sizeof tex);
            std::memcpy(nd.props, b.props, 16);
            nd.char_layout = b.layout;
            std::memcpy(nd.char_options, b.options, 16);
            std::memcpy(nd.char_lights, b.lights, sizeof nd.char_lights);
            std::memcpy(nd.char_refl, b.refl_info, 16);
            draws_.push_back(nd);
            d = &draws_.back();
        }
        for (uint16_t t : tris) char_indices_.push_back(base + t);
        d->index_count += uint32_t(tris.size());
        ++stats_.batches;
        ++stats_.char_batches;
        stats_.char_guards += b.guards;
        stats_.vertices += b.count;
        stats_.triangles += int(tris.size() / 3);
        stats_.method_batches[method]++;
        stats_.method_triangles[method] += int(tris.size() / 3);
    };
    vif.on_kick = [&](Vif1::Kick kind, uint32_t addr, uint32_t top, uint32_t) {
        note_palette();
        if (kind == Vif1::kMscal) {
            const RenderMethodInfo* m = mc_next < mc_queue.size() ? method_of_microcode(mc_queue[mc_next++]) : nullptr;
            method_name = m ? m->name : "?";
            cur_method = m;
            if (!m) {
                char b[96];
                std::snprintf(b, sizeof b, "MSCAL 0x%x sem microprograma conhecido (peça %d)", addr, part_index);
                messages_.push_back(b);
            }
            layout = car_method_layout(method_name.c_str());
            bond_layout = layout ? nullptr : bond_method_layout(method_name.c_str());
            char_layout = layout || bond_layout ? nullptr : char_method_layout(method_name.c_str());
            return;
        }
        if (kind != Vif1::kMscnt) return;
        if (char_layout) {
            if (verify_ && cur_method) char_verify_batch(cur_method->code, vif.mem, top, *char_layout, char_verify_stats_);
            CharBatch b;
            if (char_decode_batch(vif.mem, top, *char_layout, palette_bones, b)) add_char_batch(b, method_name);
        } else if (bond_layout) {
            BondBatch b;
            if (bond_decode_batch(vif.mem, top, *bond_layout, b)) {
                if (verify_ && cur_method) bond_verify_batch(cur_method->code, vif.mem, top, b, bond_verify_stats_);
                add_bond_batch(b, method_name);
            }
        } else if (layout) {
            CarBatch b;
            if (car_decode_batch(vif.mem, top, *layout, b)) {
                if (verify_ && cur_method) car_verify_batch(cur_method->code, vif.mem, top, b, verify_stats_);
                add_batch(b, false, method_name);
            }
        } else {
            fallback_batch(top);
        }
    };

    DmaBuffer dma(1 << 16);
    DrawStats ds;
    const int nsym = I.model_loader->GetCount();
    for (int si = 0; si < nsym; ++si) {
        const SymbolInfo s = I.model_loader->GetSymbol(si);
        if (!s.defined || std::strcmp(s.type, "Model") != 0) continue;
        CarPart part;
        part.name = s.name;
        part.address = s.addr;
        part.first_vertex = uint32_t(vertices_.size());
        part.first_bond_vertex = uint32_t(bond_vertices_.size());
        part_index = int(parts_.size());
        part_by_model_[s.addr] = part_index;
        const std::vector<ModelGroup> groups = Model_Groups(s.addr);
        part.groups = int(groups.size());
        // O EAGL::Model::Draw (0x002843a0) antes das primitivas: a matriz de escala das paletas (modelo +0x4c, +0x5c).
        Model_PaletteScaleMatrix(ptr_at<const float>(s.addr + 0x4c), ptr_at<const float>(s.addr + 0x5c),
                                 ctx.palette_scale);
        for (const ModelGroup& g : groups) {
            if (!g.visible) continue;
            for (uint32_t gp : g.geoprims) {
                dma.reset();
                ds.microcode_sequence.clear();
                ds.palette_uploads.clear();
                GeoPrim_Draw(gp, dma, ctx, ds);
                palette_uploads = &ds.palette_uploads;
                palette_bones.clear();
                mc_queue = ds.microcode_sequence;
                mc_next = 0;
                dma.put64(0x70000000ull);  // END
                dma.put64(0);
                stats_.dma_null_sources += dma.null_sources();
                if (dma.overflow()) {
                    ++stats_.dma_overflows;
                    messages_.push_back(std::string("a cadeia de DMA de uma primitiva não coube no buffer (peça ") + s.name + ")");
                } else {
                    vif.run_dma_chain(addr_of(dma.begin()));
                }
                dma.clear_errors();
                ++part.geoprims;
            }
        }
        part.vertex_count = uint32_t(vertices_.size()) - part.first_vertex;
        part.bond_vertex_count = uint32_t(bond_vertices_.size()) - part.first_bond_vertex;
        const bool any = part.vertex_count || part.bond_vertex_count;
        for (int c = 0; c < 3; ++c) {
            part.bbox_min[c] = any ? 1e30f : 0.0f;
            part.bbox_max[c] = any ? -1e30f : 0.0f;
        }
        for (uint32_t i = part.first_vertex; i < vertices_.size(); ++i)
            for (int c = 0; c < 3; ++c) {
                part.bbox_min[c] = std::min(part.bbox_min[c], vertices_[i].pos[c]);
                part.bbox_max[c] = std::max(part.bbox_max[c], vertices_[i].pos[c]);
            }
        for (uint32_t i = part.first_bond_vertex; i < bond_vertices_.size(); ++i)
            for (int c = 0; c < 3; ++c) {
                part.bbox_min[c] = std::min(part.bbox_min[c], bond_vertices_[i].pos[c]);
                part.bbox_max[c] = std::max(part.bbox_max[c], bond_vertices_[i].pos[c]);
            }
        stats_.groups += part.groups;
        stats_.geoprims += part.geoprims;
        parts_.push_back(part);
    }
    stats_.models = int(parts_.size());
    constants_ = render_constants;

    // As instâncias do objeto articulado ('in' do grupo 'Arti', carp.h): cada uma põe uma peça no carro.
    if (const CarpGroup* arti = I.carp_dir.group(carp_tag("Arti"))) {
        for (const CarpInstance& ci : carp_instances(I.carp.data, I.carp.size, *arti)) {
            CarInstance inst;
            std::memcpy(inst.matrix, ci.matrix, sizeof inst.matrix);
            inst.flags = ci.flags;
            inst.piece = ci.piece;
            inst.extra[0] = ci.extra[0];
            inst.extra[1] = ci.extra[1];
            for (size_t k = 0; k < parts_.size() && !ci.model.empty(); ++k)
                if (parts_[k].name == ci.model) inst.part = int(k);
            if (inst.part < 0) ++stats_.instances_without_model;
            instances_.push_back(inst);
        }
        view_lists_ = carp_view_lists(I.carp.data, I.carp.size, *arti);
    }
    stats_.instances = int(instances_.size());
    // A caixa do carro montado (todas as instâncias) e as peças que nenhuma instância usa.
    bounds(0, stats_.bbox_min, stats_.bbox_max);
    if (!instances_.empty()) {
        std::vector<bool> used(parts_.size(), false);
        for (const CarInstance& inst : instances_)
            if (inst.part >= 0) used[size_t(inst.part)] = true;
        for (bool u : used) stats_.parts_without_instance += u ? 0 : 1;
    }
    stats_.unported_ops = ds.unported_ops;
    stats_.palette_matrices = ds.palette_matrices;
    for (int op = 0; op < 32; ++op)
        if (ds.unported_by_op[op]) stats_.unported_by_op[op] = ds.unported_by_op[op];
    stats_.microcode_switches = ds.microcode_switches;
    stats_.vif_errors = int(vif.errors.size());
    for (const std::string& e : vif.errors) messages_.push_back("vif: " + e);
    if (parts_.empty()) {
        error = "nenhum __Model no objeto do carro";
        return false;
    }
    return true;
}

}  // namespace drv
