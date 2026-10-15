// Os atores no nf_driving (drive_actors.h).

#include "driving_app/drive_actors.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "driving/anim/act_actor.h"
#include "driving/anim/actor.h"
#include "driving/bigf.h"
#include "driving/car_model.h"
#include "driving/game/ai_character.h"
#include "driving/game/attributes.h"
#include "driving/render/anim_engine.h"
#include "platform/crt_compat.h"
#include "platform/ptr32.h"

namespace drv::drive_actors {

namespace an = drv::anim;

namespace {

struct State {
    const nf::DiscSource* disc = nullptr;
    std::string viv;
    Bigf* arc = nullptr;
    std::vector<std::string> ssh;  // as texturas dos personagens (data\actors\textures\*.ssh e o Actors.ssh)
    const AttributeCollection* player_attrs = nullptr;
    std::vector<an::ActDrawItem> drawn;  // os desenhos do último quadro (os DrvItem apontam para as paletas)
    int banks = 0, skeletons = 0, models = 0, weapons = 0;
    float cam[16] = {};  // a câmera do último desenho (o relatório)
    std::string errors;
};
State g;

std::string read_entry(Bigf& arc, const std::string& key) {
    const int idx = arc.find(key);
    if (idx < 0) return std::string();
    std::vector<uint8_t> b;
    arc.read(idx, b);
    return std::string(b.begin(), b.end());
}

std::string lower(std::string s) {
    for (char& c : s) c = char((c >= 'A' && c <= 'Z') ? c + 32 : c);
    return s;
}

const char* str(uint32_t p) { return p ? nf::p32_ptr<const char>(p) : nullptr; }

// O ActModelDatabase::LoadModel e o ActTextureDatabase::LoadTexture do port: o modelo do LOD 1 do personagem
// (data\actors\models\<modelo>1.dat/.rel) com as texturas da cadeia nos lugares do Actors.ssh pela primeira letra
// (h_ a cabeça 'hhhh', t_ o tronco 'tttt', b_ o corpo 'bbbb'), como o --actor.
std::shared_ptr<CarModel> load_model(const an::ActCharacterInfoData& info) {
    if (!g.disc || !g.arc) return nullptr;
    const char* model = str(info.tok_model);
    if (!model) return nullptr;
    const std::string name = std::string(model) + "1";
    auto m = std::make_shared<CarModel>();
    std::vector<std::pair<std::string, std::string>> aliases;
    for (uint32_t t : {info.tok_texture_a, info.tok_texture_b, info.tok_texture_c}) {
        const char* s = str(t);
        if (!s || std::strlen(s) < 3 || s[1] != '_') continue;
        const char c = char((s[0] >= 'A' && s[0] <= 'Z') ? s[0] + 32 : s[0]);
        if (c != 'h' && c != 't' && c != 'b') continue;
        aliases.push_back({std::string(4, c), std::string("data\\actors\\textures\\") + s + ".ssh"});
    }
    m->set_texture_aliases(aliases);
    const std::string key = "data\\actors\\models\\" + name;
    std::string err;
    if (!m->load_eagl(*g.disc, g.viv, key + ".dat", key + ".rel", g.ssh, name, err)) {
        g.errors += " modelo " + name + ": " + err + ";";
        return nullptr;
    }
    ++g.models;
    return m;
}

// O ActWeaponDatabase::LoadWeapon do port: o CARP da arma (data\actors\weapons\<nome>.crp) resolvido, as instâncias do
// 'Arti' (o RSceneObj::UseArticle do ActWeapon cria o RAnimEngine::Handle delas) e o modelo.
std::shared_ptr<an::ActWeaponResource> load_weapon(const char* name) {
    if (!g.disc || !g.arc || !name) return nullptr;
    auto r = std::make_shared<an::ActWeaponResource>();
    auto m = std::make_shared<CarModel>();
    m->set_resolve_carp(true);
    const std::string key = std::string("data\\actors\\weapons\\") + name;
    std::string err;
    // Uma arma sem modelo próprio (o grupo 'Shar': o fuzil das mãos é parte do modelo dos braços) carrega o CARP e as
    // instâncias assim mesmo: só não desenha nada.
    const bool loaded = m->load_carp(*g.disc, g.viv, key + ".crp", {key + ".ssh"}, name, err);
    if (!loaded && (m->carp_root() == nullptr || !m->parts().empty())) {
        g.errors += std::string(" arma ") + name + ": " + err + ";";
        return nullptr;
    }
    uint32_t inst = 0, n = 0, dmg = 0;
    if (!carp_instance_list(m->carp_root(), inst, n, dmg) || (loaded && n != m->instances().size())) {
        g.errors += std::string(" arma ") + name + ": sem as instâncias do 'Arti';";
        return nullptr;
    }
    r->instances = inst;
    r->count = n;
    r->damage = dmg;
    r->model = m;
    ++g.weapons;
    return r;
}

// AttributeSet::LookupVector: "x,y,z,w" (o strtod de cada um, como o lookup_float do port).
bool lookup_vector(const char* key, float out[4]) {
    if (!g.player_attrs) return false;
    std::string v;
    if (!g.player_attrs->find(key, v)) return false;
    const char* p = v.c_str();
    for (int i = 0; i < 4; ++i) {
        char* end = nullptr;
        out[i] = float(std::strtod(p, &end));
        if (end == p) break;
        p = end;
        while (*p == ',' || *p == ' ' || *p == '\t') ++p;
    }
    return true;
}

}  // namespace

bool startup(const nf::DiscSource& disc, const std::string& viv, Bigf& arc, const std::string& anim_list,
             float frame_time, std::string& error) {
    shutdown();
    g = State{};
    g.disc = &disc;
    g.viv = viv;
    g.arc = &arc;
    for (const BigfEntry& e : arc.entries()) {
        const std::string l = lower(e.name);
        if ((l.rfind("data\\actors\\textures\\", 0) == 0 && l.size() > 4 && l.compare(l.size() - 4, 4, ".ssh") == 0) ||
            l == "data\\render\\actors.ssh")
            g.ssh.push_back(e.name);
    }
    an::act_reset();
    an::ActResources& r = an::act_resources();
    r = an::ActResources{};
    r.banks.resize(18);
    r.load_model = load_model;
    r.load_weapon = load_weapon;
    // O ActManager::StartUp cria o EventTarget antes do ActManagerInit: os bancos resolvem os "event.*" por ele.
    an::act_globals().resolver = an::ActEventResolver_Construct();
    // O ActSkeletonDatabase: os nove esqueletos (data\actors\skeleton\<n>skel.o).
    for (int i = 0; i < 9; ++i) {
        auto sk = std::make_shared<an::ActSkeleton>();
        std::string e;
        if (sk->load(arc, i, e)) {
            r.skeletons[i] = sk;
            ++g.skeletons;
        }
    }
    // O ActAnimationDatabase: o ALookup.bin e os bancos da lista (entre "start" e "end"; as linhas em minúsculas).
    const std::string lookup = read_entry(arc, "data\\actors\\anims\\ALookup.bin");
    r.lookup.assign(lookup.begin(), lookup.end());
    std::string list;
    if (!anim_list.empty()) list = read_entry(arc, "data\\actors\\anims\\" + anim_list);
    if (list.empty()) list = read_entry(arc, "data\\actors\\anims\\BkDflt.txt");
    {
        std::vector<std::string> lines;
        std::string cur;
        for (char c : list) {
            if (c == '\r' || c == '\n') {
                lines.push_back(lower(cur));
                cur.clear();
            } else {
                cur += c;
            }
        }
        lines.push_back(lower(cur));
        bool in = false;
        for (const std::string& l : lines) {
            if (!in) {
                if (l.compare(0, 5, "start") == 0) in = true;
                continue;
            }
            if (l.compare(0, 3, "end") == 0) break;
            if (l.empty()) continue;
            int index = -1;
            for (int i = 0; i < 18; ++i)
                if (l == an::act_bank_name(i)) index = i;
            if (index < 0) continue;  // o PS2 avisa (0x363fd8) e segue
            auto b = std::make_shared<an::AnimBank>();
            std::string e;
            if (!b->load(arc, l, e, an::ActManager_SymbolResolver)) {
                g.errors += " banco " + l + ": " + e + ";";
                continue;
            }
            r.banks[size_t(index)] = b;
            ++g.banks;
        }
    }
    an::ActActorDatabase_Construct();
    an::ActManager_StartUp(frame_time);
    if (g.skeletons == 0) {
        error = "sem os esqueletos dos atores";
        return false;
    }
    return true;
}

void characters_init(const AttributeCollection* player_attrs, const uint8_t* camera, std::function<void()> camera_update,
                     int32_t* zoom_button) {
    g.player_attrs = player_attrs;
    AICharacterEnv& env = ai_character_env();
    env.camera = [camera]() { return camera; };
    env.camera_update = std::move(camera_update);
    env.zoom_button = zoom_button;
    env.lookup_vector = lookup_vector;
    AICharacter_Init();
}

bool upload(DrvRenderer& renderer, std::string& error) {
    for (const auto& m : an::act_resources().models)
        if (m.second && !renderer.upload(*m.second, error)) return false;
    for (const auto& w : an::act_resources().weapons)
        if (w && w->model && !w->model->parts().empty() && !renderer.upload(*w->model, error)) return false;
    return true;
}

void draw(std::vector<DrvItem>& items, const float cam[16], float fov) {
    an::act_draw_list(g.drawn);
    std::memcpy(g.cam, cam, sizeof g.cam);
    // A vista (do mundo para a câmera) e a câmera: a matriz da câmera tem as linhas ortonormais.
    float view[16] = {cam[0], cam[4], cam[8], 0, cam[1], cam[5], cam[9], 0, cam[2], cam[6], cam[10], 0, 0, 0, 0, 1};
    for (int c = 0; c < 3; ++c) view[12 + c] = -(cam[c * 4] * cam[12] + cam[c * 4 + 1] * cam[13] + cam[c * 4 + 2] * cam[14]);
    auto mul = [](const float* a, const float* b, float* out) {
        float t[16];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                t[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
        std::memcpy(out, t, sizeof t);
    };
    // Os atores da câmera: a vista escalada em x e y (o campo de visão x a escala do +0x5c).
    auto pov = [&](const an::ActDrawItem& it, const float* world, float* out) {
        std::memcpy(out, world, 64);
        static const bool no_pov = nf::get_env_narrow("NF_DRV_ACTOR_NO_POV", nullptr);  // depuração
        if (!it.pov || no_pov) return;
        const double h = double(fov) * 3.14159265358979 / 180.0;
        const double k = std::tan(h) / std::tan(h * double(it.fov_scale));
        float s[16] = {float(k), 0, 0, 0, 0, float(k), 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        mul(out, view, out);
        mul(out, s, out);
        mul(out, cam, out);
    };
    static const bool hide = nf::get_env_narrow("NF_DRV_ACTOR_HIDE", nullptr);  // depuração: sem os atores
    if (hide) return;
    for (an::ActDrawItem& it : g.drawn) {
        if (it.model) {
            DrvItem d;
            pov(it, it.world, d.world);
            d.model = it.model;
            d.bones = it.skin.empty() ? nullptr : it.skin.data();
            d.bone_count = int(it.skin.size() / 16);
            for (size_t p = 0; p < it.model->parts().size(); ++p) {
                d.part = int(p);
                items.push_back(d);
            }
        }
        for (const an::ActDrawItem::Weapon& w : it.weapons) {
            if (!w.model) continue;
            for (size_t k = 0; k < w.instances.size() && k < w.model->instances().size(); ++k) {
                const int part = w.model->instances()[k].part;
                if (part < 0) continue;
                DrvItem d;
                d.model = w.model;
                d.part = part;
                pov(it, w.instances[k].data(), d.world);
                items.push_back(d);
            }
        }
    }
}

std::string report() {
    const an::ActStats& s = an::act_stats();
    const AICharacterStats& c = ai_character_stats();
    const HandsGlobals& h = hands_globals();
    int visible = 0;
    for (uint32_t actor : an::act_actor_list())
        if (nf::p32_ptr<const an::ActActorData>(actor)->visible) ++visible;
    char b[3000];
    int hstate = -1, hweapon = -1, hanim = -1;
    float aim = 0;
    if (h.hands) {
        const AICharacterHandsData& d = *nf::p32_ptr<const AICharacterHandsData>(h.hands);
        hstate = d.base.state;
        hweapon = d.base.index;
        hanim = d.base.anim;
        aim = d.aim_fraction;
    }
    // O primeiro desenhado: o lugar da matriz do mundo e o do osso 0 da paleta (no espaço do modelo).
    float wp[3] = {0, 0, 0}, bp[3] = {0, 0, 0};
    int parts = 0;
    if (!g.drawn.empty()) {
        const an::ActDrawItem& d = g.drawn.front();
        std::memcpy(wp, d.world + 12, 12);
        if (d.skin.size() >= 16) std::memcpy(bp, d.skin.data() + 12, 12);
        parts = d.model ? int(d.model->parts().size()) : -1;
    }
    // O osso 0 no espaço da câmera (x direita, y cima, z frente).
    float vs[3] = {0, 0, 0};
    if (!g.drawn.empty()) {
        const float* w = g.drawn.front().world;
        float p[3];
        for (int j = 0; j < 3; ++j) p[j] = bp[0] * w[j] + bp[1] * w[4 + j] + bp[2] * w[8 + j] + w[12 + j] - g.cam[12 + j];
        for (int r = 0; r < 3; ++r) vs[r] = p[0] * g.cam[r * 4] + p[1] * g.cam[r * 4 + 1] + p[2] * g.cam[r * 4 + 2];
    }
    auto det3 = [](const float* m) {
        return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
    };
    float dw = 0, ds = 0;
    if (!g.drawn.empty()) {
        dw = det3(g.drawn.front().world);
        if (g.drawn.front().skin.size() >= 16) ds = det3(g.drawn.front().skin.data());
    }
    char gb[900] = "";
    if (!g.drawn.empty()) {
        const an::ActActorData& ad = *nf::p32_ptr<const an::ActActorData>(g.drawn.front().actor);
        const an::ActAnimGroup& gr = *nf::p32_ptr<const an::ActAnimGroup>(ad.group);
        const an::ActPoser& po = *nf::p32_ptr<const an::ActPoser>(ad.poser);
        const float* k = g.drawn.front().skin.data();
        std::snprintf(gb, sizeof gb,
                      " | grupo banco %d indice %d anim %08x dados %08x eventos %08x quadros %.1f | poser modo %d tempo %.2f "
                      "fim %.2f | pele0 %.3f %.3f %.3f / %.3f %.3f %.3f / %.3f %.3f %.3f",
                      gr.bank, gr.index, gr.anim, gr.anim_data, gr.events_data, double(gr.length), po.mode,
                      double(po.time), double(po.end_time), double(k[0]), double(k[1]), double(k[2]), double(k[4]),
                      double(k[5]), double(k[6]), double(k[8]), double(k[9]), double(k[10]));
        const float* pw = po.pose_to_world();
        const float* gl = po.current();
        const float* in = nf::p32_ptr<const an::ActActorMatrices>(ad.matrices)->initial;
        char xb[400];
        std::snprintf(xb, sizeof xb,
                      " | pw %.3f %.3f %.3f / %.3f %.3f %.3f / %.3f %.3f %.3f / %.3f %.3f %.3f | glob0 %.3f %.3f %.3f / "
                      "%.3f %.3f %.3f | inicial %.3f %.3f %.3f / %.3f %.3f %.3f raiz %d",
                      double(pw[0]), double(pw[1]), double(pw[2]), double(pw[4]), double(pw[5]), double(pw[6]),
                      double(pw[8]), double(pw[9]), double(pw[10]), double(pw[12]), double(pw[13]), double(pw[14]),
                      double(gl[0]), double(gl[1]), double(gl[2]), double(gl[4]), double(gl[5]), double(gl[6]),
                      double(in[0]), double(in[1]), double(in[2]), double(in[4]), double(in[5]), double(in[6]),
                      po.skel().root);
        {
            const size_t n = std::strlen(gb);
            std::snprintf(gb + n, sizeof gb - n, "%s", xb);
        }
    }
    char pb[1200];
    std::snprintf(pb, sizeof pb,
                  " | primeiro mundo %.3f,%.3f,%.3f osso0 %.3f,%.3f,%.3f na_camera %.3f,%.3f,%.3f pecas %d det %.4f %.4f",
                  double(wp[0]), double(wp[1]), double(wp[2]), double(bp[0]), double(bp[1]), double(bp[2]),
                  double(vs[0]), double(vs[1]), double(vs[2]), parts, double(dw), double(ds));
    {
        const size_t n = std::strlen(pb);
        std::snprintf(pb + n, sizeof pb - n, "%s", gb);
    }
    std::snprintf(b, sizeof b,
                  "atores esqueletos %d bancos %d modelos %d armas %d | atores %d visiveis %d desenhados %zu "
                  "atualizacoes %llu | maos estado %d arma %d animacao %d mira %.2f zoom %d disparos %llu recargas %llu "
                  "trocas %llu | eventos %llu padrao %llu fire %llu estimulos %llu | execucoes %llu humanos %llu "
                  "camera %llu som %llu menu %llu | faltando %llu guardas %llu/%llu/%llu%s%s\n",
                  g.skeletons, g.banks, g.models, g.weapons, int(an::act_actor_list().size()), visible, g.drawn.size(),
                  (unsigned long long)s.updates, hstate, hweapon, hanim, double(aim), h.is_zoomed,
                  (unsigned long long)c.hands_fired, (unsigned long long)c.reloads, (unsigned long long)c.changes,
                  (unsigned long long)s.events_fired, (unsigned long long)s.default_events,
                  (unsigned long long)s.fires, (unsigned long long)s.plays, (unsigned long long)c.executes,
                  (unsigned long long)c.humans, (unsigned long long)c.camera, (unsigned long long)c.sound,
                  (unsigned long long)c.menu_sounds, (unsigned long long)s.missing_resources,
                  (unsigned long long)s.guards, (unsigned long long)c.guards,
                  (unsigned long long)an::actor_stats().guards, pb, g.errors.empty() ? "" : (" |" + g.errors).c_str());
    return b;
}

void shutdown() {
    ai_character_reset();
    an::act_reset();
    an::act_resources() = an::ActResources{};
    g.drawn.clear();
}

}  // namespace drv::drive_actors
