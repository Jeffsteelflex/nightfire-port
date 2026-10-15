// Os atores das missões de carro (driving/anim/act_actor.h), do MIPS do DRIVING.ELF: os eventos, os controladores, o
// ActActor, o ActCharacter, o ActCharacterInfo, o ActWeapon, os bancos de dados e o ActManager. As divergências estão
// no cabeçalho e em cada função.

#include "driving/anim/act_actor.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "driving/game/missile.h"
#include "driving/game/triggers.h"
#include "driving/physics/simulation.h"
#include "driving/render/anim_engine.h"
#include "driving/render/vehicle.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv::anim {

namespace ee = ps2::ee;

namespace {

ActGlobals g_act;
ActResources g_res;
ActStats g_stats;
std::vector<ActCall> g_trace;
bool g_trace_on = false;
// (construído no primeiro uso: os registros vêm de construtores estáticos de outros arquivos, driving/game/
// ai_character.cpp, e a ordem entre arquivos não é garantida)
std::map<uint32_t, ActMatrixCallback>& callbacks() {
    static std::map<uint32_t, ActMatrixCallback> m;
    return m;
}

template <class T>
T* P(uint32_t p32) {
    return p32 ? nf::p32_ptr<T>(p32) : nullptr;
}
uint32_t r32(uint32_t p32, uint32_t off) {
    uint32_t v = 0;
    std::memcpy(&v, nf::p32_ptr<const uint8_t>(p32) + off, 4);
    return v;
}
void w32(uint32_t p32, uint32_t off, uint32_t v) { std::memcpy(nf::p32_ptr<uint8_t>(p32) + off, &v, 4); }

ActActorData& A(uint32_t actor) { return *nf::p32_ptr<ActActorData>(actor); }
ActActorMatrices& M(uint32_t actor) { return *nf::p32_ptr<ActActorMatrices>(A(actor).matrices); }
ActManagerData* manager() { return P<ActManagerData>(g_act.manager); }

// O gRigidBodyDraw (0x386b40): a instância falsa dos objetos de cena que não vêm de uma instância do CARP (a matriz
// identidade, o byte +0xd 0xff).
constexpr uint32_t kRigidBodyDraw[16] = {0x3f800000, 0, 0, 0x0000ff00, 0, 0x3f800000, 0, 0,
                                         0, 0, 0x3f800000, 0, 0, 0, 0, 0};

// Os nomes dos eventos que o ActEvents liga aos tratadores.
constexpr const char* kFireEvent = "Character_WeaponEvents_Fire_fireValue";
constexpr const char* kDropEvent = "Character_GotShotEvents_DropWeapon_dropWeaponValue";
constexpr const char* kThrowEvent = "Character_GotShotEvents_ThrowWeapon_throwWeaponValue";
constexpr const char* kPhysicsOffEvent = "Character_PhysicsOffEvents_PhysicsOff_physicsOffValue";

// Os 18 bancos da tabela 0x3264d0 (entradas de 0x2d bytes).
constexpr const char* kBankNames[18] = {"driving", "essm", "fs",  "fsb", "fsmg",  "mefs",  "ped",  "slp", "sm",
                                        "sr",      "srb",  "srmg", "ssm", "swmg", "sniper", "tfod", "ul",  "worker"};

// O strtok da newlib sobre um texto (o estado do port: o próximo começo).
struct Tok {
    char* next = nullptr;
    char* operator()(char* s) {
        if (s) next = s;
        if (!next) return nullptr;
        while (*next == '.') ++next;
        if (!*next) {
            next = nullptr;
            return nullptr;
        }
        char* start = next;
        while (*next && *next != '.') ++next;
        if (*next) *next++ = 0;
        else next = nullptr;
        return start;
    }
};

const char* str(uint32_t p) { return p ? nf::p32_ptr<const char>(p) : nullptr; }

std::shared_ptr<CarModel> find_model(const std::string& name) {
    for (auto& m : g_res.models)
        if (m.first == name) return m.second;
    return nullptr;
}
std::shared_ptr<ActWeaponResource> find_weapon(const char* name) {
    for (auto& w : g_res.weapons)
        if (w && w->name == name) return w;
    return nullptr;
}

}  // namespace

ActGlobals& act_globals() { return g_act; }
ActResources& act_resources() { return g_res; }
ActStats& act_stats() { return g_stats; }
std::vector<ActCall>& act_trace() { return g_trace; }
bool& act_trace_enabled() { return g_trace_on; }
void act_note(uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    if (g_trace_on) g_trace.push_back({fn, {a0, a1, a2, a3}});
}
void act_register_callback(uint32_t ps2_address, ActMatrixCallback fn) { callbacks()[ps2_address] = fn; }
const char* act_bank_name(int index) { return index >= 0 && index < 18 ? kBankNames[index] : ""; }

void act_reset() {
    ActActorDatabase_ShutDown();
    ActManager_ShutDown();
    g_stats = ActStats{};
    g_trace.clear();
    reset_actor_stats();
}

// ------------------------------------------------------------------------------------------------------- os eventos

// [driving] 0x0010e3a8 ActEventResolver::ActEventResolver
uint32_t ActEventResolver_Construct() {
    EventTargetData* t = new EventTargetData{};
    t->grow = 0x32;
    return nf::p32_u(t);
}

// [driving] 0x00264298 EAGLAnim::EventTarget::GetEventId
int32_t EventTarget_GetEventId(uint32_t target, const char* name, int32_t& pos) {
    const EventTargetData& t = *nf::p32_ptr<const EventTargetData>(target);
    int32_t hi = int32_t(t.count) - 1;
    pos = 0;
    if (hi < 0) return -1;
    int32_t lo = pos;
    for (;;) {
        const int32_t mid = (lo + hi) >> 1;
        const uint32_t entry = t.names + 8u * uint32_t(mid);
        const int c = std::strcmp(name, str(r32(entry, 0)));
        if (c < 1) {
            hi = mid - 1;
            if (c >= 0) return int32_t(r32(entry, 4));
        } else {
            pos = mid + 1;
        }
        if (hi < pos) break;
        lo = pos;
    }
    return -1;
}

// [driving] 0x00263e18 EAGLAnim::EventTarget::ResolveEventId
bool EventTarget_ResolveEventId(uint32_t target, const char* name, int32_t& id) {
    if (!name) return false;
    if (std::strstr(name, "event.") != name) return false;
    name += 6;
    EventTargetData& t = *nf::p32_ptr<EventTargetData>(target);
    int32_t pos = 0;
    id = EventTarget_GetEventId(target, name, pos);
    if (id >= 0) return true;
    const int32_t count = t.count;
    char* copy = new char[std::strlen(name) + 1];  // o EAGLMalloc
    std::memcpy(copy, name, std::strlen(name) + 1);
    auto names = [&](int32_t i) { return t.names + 8u * uint32_t(i); };
    if (count + 1 < t.capacity) {
        for (int32_t u = count; pos <= u; --u)
            if (u > 0) {
                w32(names(u), 0, r32(names(u - 1), 0));
                w32(names(u), 4, r32(names(u - 1), 4));
            }
    } else {
        // Cresce: os vetores novos (a capacidade + 50), os depois do lugar uma posição acima, os de antes no lugar.
        const uint32_t old_names = t.names, old_ids = t.by_id;
        t.capacity += t.grow;
        t.names = nf::p32_u(new uint32_t[size_t(t.capacity) * 2]{});
        t.by_id = nf::p32_u(new uint32_t[size_t(t.capacity)]{});
        for (int32_t u = count; pos <= u; --u)
            if (u > 0) {
                const uint32_t src = old_names + 8u * uint32_t(u - 1);
                w32(names(u), 0, r32(src, 0));
                w32(names(u), 4, r32(src, 4));
            }
        for (int32_t u = pos - 1; u >= 0; --u) {
            const uint32_t src = old_names + 8u * uint32_t(u);
            w32(names(u), 0, r32(src, 0));
            w32(names(u), 4, r32(src, 4));
        }
        if (old_ids) std::memcpy(nf::p32_ptr<uint8_t>(t.by_id), nf::p32_ptr<const uint8_t>(old_ids), size_t(count) * 4);
        delete[] P<uint32_t>(old_names);
        delete[] P<uint32_t>(old_ids);
    }
    w32(names(pos), 0, nf::p32_u(copy));
    w32(t.by_id, 4u * uint32_t(count), nf::p32_u(copy));
    w32(names(pos), 4, uint32_t(count));
    t.count = uint16_t(count + 1);
    id = int32_t(t.count) - 1;
    return true;
}

// [driving] 0x0010e448 ActEvents::ActEvents
uint32_t ActEvents_Construct(uint32_t target) {
    ActEventsData* e = new ActEventsData{};
    uint32_t* words = reinterpret_cast<uint32_t*>(e);
    auto handler = [](uint32_t vtable) { return nf::p32_u(new ActEventHandler{0, vtable}); };
    e->def = handler(kVtDefaultEvent);
    const int32_t count = target ? int32_t(nf::p32_ptr<const EventTargetData>(target)->count) : 0;
    // Todos os números no padrão. Mais de 32 eventos: o PS2 escreve nos campos depois da tabela (aqui até o fim do
    // objeto; além, guardado).
    for (int32_t i = count - 1; i >= 0; --i) {
        if (i < 37) words[i] = e->def;
        else ++g_stats.guards;
    }
    e->fire = handler(kVtFireEvent);
    e->drop = handler(kVtDropWeaponEvent);
    e->height = handler(kVtHeightCalcEvent);
    auto bind = [&](const char* name, uint32_t h) {
        if (!target) return;
        int32_t pos;
        const int32_t id = EventTarget_GetEventId(target, name, pos);
        if (id == -1) return;
        if (id >= 0 && id < 37) words[id] = h;
        else ++g_stats.guards;
    };
    bind(kFireEvent, e->fire);
    bind(kDropEvent, e->drop);
    bind(kThrowEvent, e->drop);
    bind(kPhysicsOffEvent, e->height);
    e->current = 0;
    return nf::p32_u(e);
}

// [driving] 0x0010e5b8 ActEvents::~ActEvents
void ActEvents_Destruct(uint32_t events) {
    ActEventsData* e = P<ActEventsData>(events);
    if (!e) return;
    delete P<ActEventHandler>(e->drop);
    delete P<ActEventHandler>(e->fire);
    delete P<ActEventHandler>(e->def);
    delete P<ActEventHandler>(e->height);  // o PS2 não o solta (vaza 8 bytes)
    delete e;
}

// [driving] 0x0010e610 ActEvents::SetCurrentActor
void ActEvents_SetCurrentActor(uint32_t events, uint32_t actor) {
    if (ActEventsData* e = P<ActEventsData>(events)) e->current = actor;
    g_act.current = actor;
}

// A chamada de um tratador (a entrada +0xc da tabela virtual): os quatro tratadores do ActEvents.
void ActEvent_Dispatch(uint32_t handler, float /*time*/, uint32_t event, uint32_t /*user*/) {
    const uint32_t vt = nf::p32_ptr<const ActEventHandler>(handler)->vtable;
    const uint32_t actor = g_act.current;
    ++actor_stats().events_fired;
    switch (vt) {
    case kVtDefaultEvent:
        // 0x0010e738 DefaultEventHandler::HandleEvent: nada.
        ++g_stats.default_events;
        return;
    case kVtFireEvent:
        // 0x0010e7c8 FireEvent::HandleEvent: ActActor::Fire do ator corrente (os três valores do evento em
        // f13-f15, que o Fire não usa).
        ++g_stats.events_fired;
        if (!actor) {
            ++g_stats.guards;
            return;
        }
        ActActor_Fire(actor);
        return;
    case kVtDropWeaponEvent: {
        // 0x0010e880 DropWeaponEvent::HandleEvent: ActActor::DropWeapon(o valor +0xc do evento).
        ++g_stats.events_fired;
        if (!actor) {
            ++g_stats.guards;
            return;
        }
        float v;
        std::memcpy(&v, nf::p32_ptr<const uint8_t>(event) + 0xc, 4);
        ActActor_DropWeapon(actor, v);
        return;
    }
    case kVtHeightCalcEvent:
        // 0x0010e930 HeightCalcEvent::HandleEvent: o ator deixa de procurar o chão.
        ++g_stats.events_fired;
        if (!actor) {
            ++g_stats.guards;
            return;
        }
        A(actor).floor_check = 0;
        return;
    default:
        ++g_stats.guards;  // outra tabela (o PS2 chamaria o que estivesse nela)
    }
}

// ----------------------------------------------------------------------------------------- os controladores

// [driving] 0x00107418 StandardAnimationController::StandardAnimationController
uint32_t StandardAnimationController_Construct(uint32_t group, uint32_t poser, int32_t bank, int32_t index,
                                               uint32_t matrix) {
    // 0x00107338 AnimationController::AnimationController
    AnimControllerData* c = new AnimControllerData{};
    c->group = group;
    c->poser = poser;
    c->vtable = kVtAnimController;
    c->matrix = matrix;
    c->vtable = kVtStandardController;
    ++g_stats.controllers;
    nf::p32_ptr<ActAnimGroup>(group)->ChangeAnimation(bank, index);
    nf::p32_ptr<ActPoser>(poser)->SetNormalAnimation(nf::p32_ptr<ActAnimGroup>(group), 0.0f, P<float>(matrix));
    return nf::p32_u(c);
}

void AnimController_Update(uint32_t controller) {
    const AnimControllerData& c = *nf::p32_ptr<const AnimControllerData>(controller);
    if (c.vtable == kVtStandardController) {
        // 0x00107490 StandardAnimationController::Update
        nf::p32_ptr<ActPoser>(c.poser)->AdvanceTime();
        return;
    }
    ++g_stats.other_controllers;  // os controladores de mistura não são portados (o port não os cria)
}

void AnimController_Destruct(uint32_t controller) {
    // 0x0010bba8 StandardAnimationController::~StandardAnimationController (o ~AnimationController 0x00107358)
    delete P<AnimControllerData>(controller);
}

// [driving] 0x00107388 AnimationController::GetCurrentFrame
float AnimController_GetCurrentFrame(uint32_t controller) {
    return nf::p32_ptr<const ActPoser>(nf::p32_ptr<const AnimControllerData>(controller)->poser)->time;
}
// [driving] 0x00107398 AnimationController::GetTotalFrames
float AnimController_GetTotalFrames(uint32_t controller) {
    return nf::p32_ptr<const ActPoser>(nf::p32_ptr<const AnimControllerData>(controller)->poser)->end_time;
}
// [driving] 0x001073a8 AnimationController::GetCurrentTime (o quadro x 1/20)
float AnimController_GetCurrentTime(uint32_t controller) {
    const ActPoser& p = *nf::p32_ptr<const ActPoser>(nf::p32_ptr<const AnimControllerData>(controller)->poser);
    return ee::mul(p.time, p.seconds_per_frame);
}
// [driving] 0x001073c0 AnimationController::GetTotalTime
float AnimController_GetTotalTime(uint32_t controller) {
    const ActPoser& p = *nf::p32_ptr<const ActPoser>(nf::p32_ptr<const AnimControllerData>(controller)->poser);
    return ee::mul(p.end_time, p.seconds_per_frame);
}
// [driving] 0x001073d8 AnimationController::GetRemainingTime
float AnimController_GetRemainingTime(uint32_t controller) {
    const float total = AnimController_GetTotalTime(controller);
    const float now = AnimController_GetCurrentTime(controller);
    return ee::sub(total, now);
}

// ------------------------------------------------------------------------------------------------ a animação

// [driving] 0x0010c928 ActAnimGroup::ChangeAnimation(int, int)
void ActAnimGroup::ChangeAnimation(int32_t bank_, int32_t index_) {
    bank = bank_;
    index = index_;
    uint32_t pose = 0, ev = 0;
    ActAnimationDatabase_GetAnimation(bank_, index_, pose, ev);
    ChangeAnimationData(pose, ev);
}

// [driving] 0x0010c1d8 PrivateData::GetAnimation(int banco, int índice, AnimMemoryMap **pose, **eventos)
void ActAnimationDatabase_GetAnimation(int32_t bank, int32_t index, uint32_t& pose, uint32_t& events) {
    // 0x0010c5e0 ActAnimationDatabase::GetAnimation (o PrivateData do +0)
    pose = events = 0;
    ActManagerData* m = manager();
    ActAnimationDatabaseData* db = m ? P<ActAnimationDatabaseData>(m->animations) : nullptr;
    if (!db || !db->lookup || bank < 0 || bank >= 18) {
        ++g_stats.guards;  // o PS2 leria a tabela de um banco de dados nulo ou fora dela
        return;
    }
    const uint32_t lookup = db->lookup;
    const uint32_t start = r32(lookup, 4u * uint32_t(bank));
    const uint32_t entry = lookup + 4u * (start + 2u * uint32_t(index));
    if (4u * (start + 2u * uint32_t(index)) + 8 > db->lookup_size) {
        ++g_stats.guards;
        return;
    }
    const int32_t ev = int32_t(r32(entry, 0)), po = int32_t(r32(entry, 4));
    const ActBankInfo& b = db->banks[bank];
    // 0x0010c798 EAGLAnim::FnDefaultAnimBank::GetAnim(int) (pela tabela virtual +0x1c): sem conferir o índice.
    auto get = [&](int32_t i) -> uint32_t {
        if (!b.bank || i < 0 || i >= int32_t(r32(b.bank, 4))) {
            ++g_stats.guards;
            return 0;
        }
        return r32(r32(b.bank, 0xc), 4u * uint32_t(i));
    };
    if (po != -1) pose = get(po);
    if (ev != -1) events = get(ev);
}

uint32_t ActAnimationDatabase_Build() {
    ActAnimationDatabaseData* db = new ActAnimationDatabaseData{};
    for (int i = 0; i < 18; ++i) {
        ActBankInfo& b = db->banks[i];
        const AnimBank* bank = size_t(i) < g_res.banks.size() ? g_res.banks[size_t(i)].get() : nullptr;
        if (!bank) continue;
        // 0x0010bf90 BankInfo::Load (os arquivos já carregados pelo port: o banco, o FnDefaultAnimBank, o
        // número de animações e o nome) e 0x0010c110 PrivateData::LoadBank (as contagens).
        std::snprintf(b.name, sizeof b.name, "%s", kBankNames[i]);
        b.bank = bank->address();
        uint32_t* def = new uint32_t[2]{0x00364028u, b.bank};
        b.def_bank = nf::p32_u(def);
        b.loader = nf::p32_u(bank->object().loader());
        b.file = nf::p32_u(bank->object().data());
        b.num_anims = bank->num_anims();
        ++db->loaded;
        db->total += b.num_anims;
    }
    if (!g_res.lookup.empty()) {
        db->lookup = nf::p32_u(g_res.lookup.data());
        db->lookup_size = uint32_t(g_res.lookup.size());
    }
    return nf::p32_u(db);
}

// ------------------------------------------------------------------------------------------------------ o gerente

// [driving] 0x00110f10 ActManager::SymbolResolver
uint32_t ActManager_SymbolResolver(const char* name, bool& found) {
    found = true;
    if (std::strncmp(name, "event.", 6) == 0) {
        int32_t id = 0;
        if (g_act.resolver && EventTarget_ResolveEventId(g_act.resolver, name, id)) return uint32_t(id);
    }
    found = false;
    return 0;
}

// [driving] 0x00110900 ActManager::StartUp
void ActManager_StartUp(float frame_time) {
    if (!g_act.resolver) g_act.resolver = ActEventResolver_Construct();
    // 0x00110a88 ActManager::ActManager
    ActManagerData* m = new ActManagerData{};
    ActRenderOptions_SetAlpha(m->render_options, 1.0f);
    ActRenderOptions_SetShadowDarkness(m->render_options, ps2::ffloat(0x3e19999au));
    g_act.manager = nf::p32_u(m);
    // 0x00110ae0 ActManager::ActManagerInit: o tempo de quadro; os bancos de dados (os esqueletos 0-8 do
    // ActSkeletonDatabase 0x00114c78, as animações do ActAnimationDatabase); o ActEvents com o EventTarget.
    m->frame_time = frame_time;
    uint32_t* skels = new uint32_t[9]{};
    for (int i = 0; i < 9; ++i) {
        if (g_res.skeletons[i]) skels[i] = nf::p32_u(&g_res.skeletons[i]->d);
        else ++g_stats.missing_resources;
    }
    m->skeletons = nf::p32_u(skels);
    m->animations = ActAnimationDatabase_Build();
    m->events = ActEvents_Construct(g_act.resolver);
}

// [driving] 0x001109f8 ActManager::ShutDown
void ActManager_ShutDown() {
    ActManagerData* m = manager();
    if (m) {
        ActEvents_Destruct(m->events);
        if (ActAnimationDatabaseData* db = P<ActAnimationDatabaseData>(m->animations)) {
            for (ActBankInfo& b : db->banks) delete[] P<uint32_t>(b.def_bank);
            delete db;
        }
        delete[] P<uint32_t>(m->skeletons);
        delete m;
    }
    g_act.manager = 0;
}

// ------------------------------------------------------------------------------------------------ o personagem

// [driving] 0x0010de10 ActRenderOptions::SetAlpha
void ActRenderOptions_SetAlpha(ActRenderOptionsData& o, float alpha) {
    const float a = ee::mul(alpha, 255.0f);
    const float half = ee::lt(a, 0.0f) ? -0.5f : 0.5f;
    o.alpha = ee::cvt_w(ee::add(half, a));
}

// [driving] 0x0010de58 ActRenderOptions::SetShadowDarkness
void ActRenderOptions_SetShadowDarkness(ActRenderOptionsData& o, float d) {
    o.shadow[0] = d;
    o.one = 1.0f;
    o.shadow[2] = d;
    o.shadow[1] = d;
}

// [driving] 0x0010de98 ActCharacterInfo::ActCharacterInfo
void ActCharacterInfo_Construct(ActCharacterInfoData& info, const char* text) {
    {
        const size_t n = std::min(std::strlen(text), sizeof info.text - 1);
        std::memcpy(info.text, text, n);
        info.text[n] = 0;
    }
    if (std::strlen(text) >= sizeof info.text) ++g_stats.guards;  // o strcpy do PS2 passaria do campo
    Tok tok;
    char* t_skel = tok(info.text);
    char* t_model = tok(nullptr);
    info.tok_skeleton = t_skel ? nf::p32_u(t_skel) : 0u;
    info.tok_model = t_model ? nf::p32_u(t_model) : 0u;
    if (!t_skel || !t_model) {
        ++g_stats.guards;  // o PS2 leria a cadeia nula
        return;
    }
    char c = t_model[0];
    if (c >= 'A' && c <= 'Z') c = char(c + 0x20);
    if (c == 'h') info.type = 0;
    else if (c == 'p') info.type = 1;
    else if (c == 'c') info.type = 2;
    else if (c == 'a') info.type = 3;
    else ++g_stats.guards;  // fica o que o FastAlloc deixou
    info.skeleton = std::strlen(t_skel) == 2 ? (t_skel[0] - '0') * 10 - '0' + t_skel[1] : t_skel[0] - '0';
    std::snprintf(info.model_path, sizeof info.model_path, "data\\actors\\models\\%sy.xxx", t_model);
    info.model_suffix = nf::p32_u(info.model_path + std::strlen(info.model_path) - 5);
    auto tex_path = [](const char* name) {
        char* p = new char[100]{};
        std::snprintf(p, 100, "data\\actors\\textures\\%s.ssh", name ? name : "");
        return nf::p32_u(p);
    };
    if (info.type == 0 || info.type == 2) {
        info.tok_texture_a = 0;
        char* b = tok(nullptr);
        char* cc = tok(nullptr);
        info.tok_texture_b = b ? nf::p32_u(b) : 0u;
        info.tok_texture_c = cc ? nf::p32_u(cc) : 0u;
        info.path_a = 0;
        info.path_b = tex_path(b);
        info.path_c = tex_path(cc);
    } else {
        char* a = tok(nullptr);
        info.tok_texture_b = info.tok_texture_c = 0;
        info.path_b = info.path_c = 0;
        info.tok_texture_a = a ? nf::p32_u(a) : 0u;
        info.path_a = tex_path(a);
    }
    char* s = tok(nullptr);
    info.tok_scale = s ? nf::p32_u(s) : 0u;
    int32_t pct = 100;
    if (!s) {
        ++g_stats.guards;  // o strlen do PS2 leria a cadeia nula
    } else {
        const size_t n = std::strlen(s);
        if (n == 2) pct = (s[0] - '0') * 10 - '0' + s[1];
        else if (n == 1) pct = s[0] - '0';
        else if (n == 3) pct = (s[0] - '0') * 100 + (s[1] - '0') * 10 - '0' + s[2];
    }
    info.scale = ee::div(ee::cvt_s(pct), 100.0f);
    info.inv_scale = ee::div(1.0f, info.scale);
}

// [driving] 0x0010e2a0 ActCharacterInfo::~ActCharacterInfo
void ActCharacterInfo_Destruct(ActCharacterInfoData& info) {
    delete[] P<char>(info.path_a);
    delete[] P<char>(info.path_b);
    delete[] P<char>(info.path_c);
    info.path_a = info.path_b = info.path_c = 0;
}

// [driving] 0x0010e318 ActCharacterInfo::GetModelFileName
const char* ActCharacterInfo_GetModelFileName(ActCharacterInfoData& info, int lod) {
    char* s = nf::p32_ptr<char>(info.model_suffix);
    s[0] = char(lod + '1');
    s[1] = '.';
    s[2] = 'd';
    s[3] = 'a';
    s[4] = 't';
    return info.model_path;
}

namespace {

// O nome do modelo (o LOD 1) de um ActCharacterInfo: "<modelo>1".
std::string model_name_of(const ActCharacterInfoData& info) {
    return std::string(info.tok_model ? str(info.tok_model) : "") + "1";
}

// O ActWeaponDatabase::UseWeapon (0x001171b0) do port: a arma pelo nome (o GetReference do URefCounter); sem ela o PS2
// leria o WeaponInfo nulo (guardado).
uint32_t use_weapon(const char* name, uint32_t flags) {
    std::shared_ptr<ActWeaponResource> r = find_weapon(name);
    if (!r) {
        ++g_stats.missing_resources;
        return 0;
    }
    return ActWeapon_Construct(nf::p32_u(r.get()), flags);
}

// [driving] 0x0010d0d0 ActCharacter::StopUsingResources: as armas (o ActWeaponDatabase::StopUsingWeapon 0x00117208 chama
// o destrutor), as texturas e o modelo (os bancos do port não contam referências).
void stop_using_resources(ActCharacterData& c) {
    if (c.weapon[0]) ActWeapon_Destruct(c.weapon[0]);
    if (c.weapon[1]) ActWeapon_Destruct(c.weapon[1]);
    c.weapon[0] = c.weapon[1] = 0;
}

// [driving] 0x0010ce20 ActCharacter::ChangeCharacter(char *, char *, char *, unsigned int bandeiras das armas)
void change_character(ActCharacterData& c, const char* character, const char* w0, const char* w1, uint32_t flags) {
    if (c.info) {
        stop_using_resources(c);
        ActCharacterInfo_Destruct(*P<ActCharacterInfoData>(c.info));
        delete P<ActCharacterInfoData>(c.info);
    }
    ActCharacterInfoData* info = new ActCharacterInfoData{};
    ActCharacterInfo_Construct(*info, character);
    c.info = nf::p32_u(info);
    // ActModelDatabase::UseModel (0x00111828): o modelo do port pelo nome (o LoadCharacter o carregou).
    std::shared_ptr<CarModel> m = find_model(model_name_of(*info));
    c.model = m ? nf::p32_u(m.get()) : 0u;
    if (!m) ++g_stats.missing_resources;
    c.texture[0] = c.texture[1] = c.texture[2] = 0;
    c.base_texture = 0;  // ActTextureDatabase::UseTexture("hhhh") e as três (o CarModel as tem)
    for (int k = 0; k < 2; ++k) {
        const char* w = k == 0 ? w0 : w1;
        if (!w) {
            std::snprintf(c.weapon_name[k], sizeof c.weapon_name[k], "NO WEAPON!");
            c.weapon[k] = 0;
        } else {
            std::snprintf(c.weapon_name[k], sizeof c.weapon_name[k], "%s", w);
            c.weapon[k] = use_weapon(w, flags);
        }
    }
}

}  // namespace

// [driving] 0x0010cc60 ActCharacter::ActCharacter
uint32_t ActCharacter_Construct(int32_t no_callback, const char* character, const char* weapon0, const char* weapon1) {
    ActCharacterData* c = new ActCharacterData{};
    c->info = 0;
    c->no_callback = no_callback;
    ActRenderOptionsData* ro = new ActRenderOptionsData{};
    ActRenderOptions_SetAlpha(*ro, 1.0f);
    ActRenderOptions_SetShadowDarkness(*ro, ps2::ffloat(0x3e19999au));
    c->render_options = nf::p32_u(ro);
    if (ActManagerData* m = manager()) {
        c->models = m->models;
        c->textures = m->textures;
        c->weapons = m->weapons;
    }
    change_character(*c, character, weapon0, weapon1, 0);
    const int32_t skel = nf::p32_ptr<const ActCharacterInfoData>(c->info)->skeleton;
    if (skel == 5 || skel == 6 || skel == 7 || skel == 8) change_character(*c, character, weapon0, weapon1, 2);
    return nf::p32_u(c);
}

// [driving] 0x0010d188 ActCharacter::~ActCharacter
void ActCharacter_Destruct(uint32_t ch) {
    ActCharacterData* c = P<ActCharacterData>(ch);
    if (!c) return;
    delete P<ActRenderOptionsData>(c->render_options);
    stop_using_resources(*c);
    if (c->info) {
        ActCharacterInfo_Destruct(*P<ActCharacterInfoData>(c->info));
        delete P<ActCharacterInfoData>(c->info);
    }
    delete c;
}

// [driving] 0x0010cb88 ActCharacter::LoadCharacter
void ActCharacter_LoadCharacter(const char* character, const char* weapon0, const char* weapon1) {
    ActCharacterInfoData info{};
    ActCharacterInfo_Construct(info, character);
    // ActModelDatabase::LoadModel (0x00111748) e ActTextureDatabase::LoadTexture (0x00114fa0): o CarModel do port.
    const std::string name = model_name_of(info);
    if (!find_model(name)) {
        std::shared_ptr<CarModel> m = g_res.load_model ? g_res.load_model(info) : nullptr;
        g_res.models.push_back({name, m});
        if (!m) ++g_stats.missing_resources;
    }
    // ActWeaponDatabase::LoadWeapon (0x00117060): o CARP da arma (uma vez por nome).
    for (const char* w : {weapon0, weapon1}) {
        if (!w || find_weapon(w)) continue;
        std::shared_ptr<ActWeaponResource> r = g_res.load_weapon ? g_res.load_weapon(w) : nullptr;
        if (!r) {
            ++g_stats.missing_resources;
            continue;
        }
        r->name = w;
        g_res.weapons.push_back(r);
    }
    ActCharacterInfo_Destruct(info);
}

// [driving] 0x0010d258 ActCharacter::PlayEvent(int arma, unsigned int estímulo)
void ActCharacter_PlayEvent(uint32_t ch, int32_t weapon, uint32_t stimulus) {
    const uint32_t w = r32(ch, 0x24 + 4u * uint32_t(weapon));
    if (w) ActWeapon_PlayEvent(w, stimulus);
}

// [driving] 0x0010d308 ActCharacter::GetBaseScaleFactor (0,1)
static float GetBaseScaleFactor() { return ps2::ffloat(0x3dcccccdu); }

// [driving] 0x0010d298 ActCharacter::GetScaleFactors(float &, float &)
void ActCharacter_GetScaleFactors(uint32_t ch, float& scale, float& inv) {
    const ActCharacterInfoData& info = *P<ActCharacterInfoData>(nf::p32_ptr<ActCharacterData>(ch)->info);
    scale = ee::mul(info.scale, GetBaseScaleFactor());
    inv = ee::div(info.inv_scale, GetBaseScaleFactor());
}

// [driving] 0x0010d1f0 ActCharacter::SetAlpha(float)
void ActCharacter_SetAlpha(uint32_t ch, float alpha) {
    ActRenderOptionsData& o = *P<ActRenderOptionsData>(nf::p32_ptr<ActCharacterData>(ch)->render_options);
    const float a = ee::mul(alpha, 255.0f);
    const float half = ee::lt(a, 0.0f) ? -0.5f : 0.5f;
    o.alpha = ee::cvt_w(ee::add(half, a));
}

// [driving] 0x0010dc00 ActCharacter::SetWeaponBone(int, MATRIX4 &)
void ActCharacter_SetWeaponBone(uint32_t ch, int32_t weapon, const float* m) {
    const uint32_t w = r32(ch, 0x24 + 4u * uint32_t(weapon));
    if (!w) return;
    // 0x001c9258 RSceneObj::SetTransform (o RSceneObj da arma, sem física): as linhas xyz na instância falsa e
    // o UpdatePosition (a entrada +0x7c, com 1: o nó da árvore do mapa, que o port não usa para as armas).
    ActWeaponData& a = *nf::p32_ptr<ActWeaponData>(w);
    if (a.scene.physics != 0) return;
    uint8_t* inst = nf::p32_ptr<uint8_t>(a.scene.instance);
    for (int r = 0; r < 4; ++r) std::memcpy(inst + 16 * r, m + 4 * r, 12);
    ++g_stats.scene_tree;
}

// [driving] 0x0010d048 ActCharacter::SetWeaponVelocity(COORD3 &)
void ActCharacter_SetWeaponVelocity(uint32_t ch, const float* vel) {
    const ActCharacterData& c = *nf::p32_ptr<const ActCharacterData>(ch);
    const float k = ps2::ffloat(0x3c888889u);  // 1/60
    for (int i = 0; i < 2; ++i) {
        if (!c.weapon[i]) continue;
        ActWeaponData& w = *nf::p32_ptr<ActWeaponData>(c.weapon[i]);
        w.velocity[0] = ee::mul(vel[0], k);
        w.velocity[1] = ee::mul(vel[1], k);
        w.velocity[2] = ee::mul(vel[2], k);
    }
}

// [driving] 0x0010cfe8 ActCharacter::SetWeaponsTransformToWorldSpace(MATRIX4 &, MATRIX4 &)
void ActCharacter_SetWeaponsTransformToWorldSpace(uint32_t ch, const float* bone, const float* world) {
    const ActCharacterData& c = *nf::p32_ptr<const ActCharacterData>(ch);
    if (c.weapon[0]) ActWeapon_SetTransformToWorldSpace(c.weapon[0], bone, world);
    if (c.weapon[1]) ActWeapon_SetTransformToWorldSpace(c.weapon[1], bone, world);
}

// [driving] 0x0010cd78 ActCharacter::ShowWeapon(int)
void ActCharacter_ShowWeapon(uint32_t ch, int32_t weapon) {
    const uint32_t w = r32(ch, 0x24 + 4u * uint32_t(weapon));
    if (!w) return;
    ActWeaponData& a = *nf::p32_ptr<ActWeaponData>(w);
    a.scene.flags |= 0x1000000u;  // 0x001c8b90 RSceneObj::Show
    a.flags |= 1u;
}

// [driving] 0x0010cdc8 ActCharacter::HideWeapon(int)
void ActCharacter_HideWeapon(uint32_t ch, int32_t weapon) {
    const uint32_t w = r32(ch, 0x24 + 4u * uint32_t(weapon));
    if (!w) return;
    ActWeaponData& a = *nf::p32_ptr<ActWeaponData>(w);
    a.scene.flags &= 0xfeffffffu;  // 0x001c8b78 RSceneObj::Hide
    a.flags &= ~1u;
}

// ------------------------------------------------------------------------------------------------------ a arma

// [driving] 0x001162a8 ActWeapon::ActWeapon(ActWeaponAux *, unsigned int bandeiras)
uint32_t ActWeapon_Construct(uint32_t aux, uint32_t flags) {
    ActWeaponData* w = new ActWeaponData{};
    std::memcpy(w->instance, kRigidBodyDraw, 0x40);
    // RSceneObj::RSceneObj(CARP::Instance *) (0x001c7098) sobre a instância falsa: os campos (o byte +0x1a 100, o bit
    // 24), sem a árvore do mapa (contado: o PS2 o põe no nó da origem, raio 10) e sem o RPathEngine (nenhum caminho).
    RSceneObj& s = w->scene;
    s.instance = nf::p32_u(w->instance);
    s.flags = 0x01640000u;
    s.vtable = kVtActWeaponScene;
    ++g_stats.scene_tree;
    w->flags = flags;
    w->aux = aux;
    // RSceneObj::UseArticle(o 'Arti' do CARP, 0) (0x001c7658) -> UseInstanceList (0x001c7940): o Handle das instâncias
    // do artigo e o bit 25; o raio e os efeitos (ComputeBoundingRadius, InitEffects) não são portados.
    const ActWeaponResource* r = P<ActWeaponResource>(aux);
    if (r && r->instances && r->count) {
        s.article = AnimHandle_Create(r->count, uint32_t(simulation().steps), r->instances, r->damage, nf::p32_u(&s));
        s.flags |= 0x2000000u;
    } else {
        ++g_stats.missing_resources;
    }
    for (auto& f : w->flash) f.start = 0;
    w->flags |= 1u;
    return nf::p32_u(w);
}

// [driving] 0x00116410 ActWeapon::~ActWeapon (com o ~RSceneObj 0x001c7270: o Handle)
void ActWeapon_Destruct(uint32_t weapon) {
    ActWeaponData* w = P<ActWeaponData>(weapon);
    if (!w) return;
    delete[] P<float>(w->bone_matrix);
    delete[] P<float>(w->world_matrix);
    if (w->scene.article) AnimHandle_Destroy(w->scene.article);
    delete w;
}

// [driving] 0x00116498 ActWeapon::SetTransformToWorldSpace(MATRIX4 &, MATRIX4 &)
void ActWeapon_SetTransformToWorldSpace(uint32_t weapon, const float* bone, const float* world) {
    ActWeaponData& w = *nf::p32_ptr<ActWeaponData>(weapon);
    if (!w.bone_matrix) w.bone_matrix = nf::p32_u(new float[16]{});
    if (!w.world_matrix) w.world_matrix = nf::p32_u(new float[16]{});
    std::memcpy(P<float>(w.bone_matrix), bone, 64);
    std::memcpy(P<float>(w.world_matrix), world, 64);
}

// [driving] 0x00116608 ActWeapon::SetOwner(PhysicsObject *): `owner` é a imagem do PS2 (p32) e `owned_by_player` o
// PhysicsObject::IsOwnedBy(Simulation::GetPlayerObject) dela.
void ActWeapon_SetOwner(uint32_t weapon, uint32_t owner, bool owned_by_player) {
    ActWeaponData& w = *nf::p32_ptr<ActWeaponData>(weapon);
    w.owner = owner;
    if (owned_by_player) w.flags |= 2u;
    else w.flags &= 0xfffffffdu;
}

// [driving] 0x00116678 ActWeapon::SetEventDynamicData: o do RSceneObj e o ActWeapon no EventDynamicData +0x34.
void ActWeapon_SetEventDynamicData(uint32_t weapon) {
    RSceneObj_SetEventDynamicData(nf::p32_u(&nf::p32_ptr<ActWeaponData>(weapon)->scene));
    trigger_manager().event_data.weapon = weapon;  // 0x32e73c
}

// [driving] 0x001166a8 ActWeapon::PlayEvent(unsigned int): o estímulo em todos os sistemas do Handle da arma.
void ActWeapon_PlayEvent(uint32_t weapon, uint32_t stimulus) {
    ++g_stats.plays;
    const ActWeaponData& w = *nf::p32_ptr<const ActWeaponData>(weapon);
    const uint32_t steps = uint32_t(simulation().steps);
    act_note(0x001a2868, weapon, stimulus, steps, 1);
    if (!w.scene.article) {
        ++g_stats.guards;  // o PS2 leria o Handle nulo
        return;
    }
    AnimHandle_ProcessStimuliAll(w.scene.article, stimulus, steps, 1);
}

// [driving] 0x00116cb0 ActWeapon::TransformToWorldSpace(MATRIX4 &)
void ActWeapon_TransformToWorldSpace(uint32_t weapon, float* m) {
    const ActWeaponData& w = *nf::p32_ptr<const ActWeaponData>(weapon);
    if (w.bone_matrix && w.world_matrix) {
        MATRIX4_mult(m, P<float>(w.bone_matrix), m);
        MATRIX4_mult(m, P<float>(w.world_matrix), m);
    }
}

// [driving] 0x00116df0 ActWeapon::TransformMatrixToWorldSpace(MATRIX4 &)
void ActWeapon_TransformMatrixToWorldSpace(uint32_t weapon, float* m) { ActWeapon_TransformToWorldSpace(weapon, m); }

uint32_t ActWeapon_FromScene(uint32_t scene) {
    if (!scene) return 0;
    if (nf::p32_ptr<const RSceneObj>(scene)->vtable != kVtActWeaponScene) return 0;
    return scene - 0x40;
}

// O RSceneObj::GetInstancePosition(unsigned int, MATRIX4 &, bool) (0x001c9368) para o RSceneObj de uma arma (com a
// TransformToWorldSpace dela, a entrada +0x4c).
bool ActWeapon_GetInstancePosition(uint32_t scene, int32_t index, float m[4][4]) {
    const uint32_t weapon = ActWeapon_FromScene(scene);
    if (!weapon) return false;
    const RSceneObj& s = *nf::p32_ptr<const RSceneObj>(scene);
    // RSceneObj::GetTransform (0x001c9190): a instância com o w das linhas 0, 0, 0, 1.
    std::memcpy(m, nf::p32_ptr<const uint8_t>(s.instance), 64);
    m[0][3] = m[1][3] = m[2][3] = 0.0f;
    m[3][3] = 1.0f;
    const uint32_t h = s.article;
    if (h && uint32_t(index) < nf::p32_ptr<const uint8_t>(h)[0x1c]) {
        // A entrada +0x5c (SetProcAnimState: vazio no RSceneObj; os ossos do gProcAnimFunc ficam os do último objeto,
        // que as instâncias das armas, dos tipos 0x00-0x63 do RAnimEngine::EvaluateInstance, não leem) e a
        // GetInstanceMatrix da cópia com os estados do Handle (+8).
        ProcAnimContext ctx;
        ctx.states = r32(h, 8) ? nf::p32_ptr<const uint8_t>(r32(h, 8)) : nullptr;
        uint32_t raw[16];
        std::memcpy(raw, nf::p32_ptr<const uint8_t>(h) + 0x40 + size_t(index) * 0x40, 64);
        float im[4][4];
        if (GetInstanceMatrix(raw, ctx, im)) MATRIX4_mult(&im[0][0], &m[0][0], &m[0][0]);
        else ++g_stats.guards;
    }
    // A entrada +0x4c (TransformToWorldSpace do ActWeapon).
    ActWeapon_TransformToWorldSpace(weapon, &m[0][0]);
    return true;
}

// [driving] 0x001cae00 RSceneObj::LocateFX, 0x001c95f0 RSceneObj::TransformMatrixByInstancePosition e 0x001ca8e0
// RSceneObj::AdjustEffectPosition (o RSceneObj de uma arma; o mundo ligado)
bool ActWeapon_LocateFX(uint32_t scene, uint32_t effect, float m[4][4]) {
    const uint32_t weapon = ActWeapon_FromScene(scene);
    if (!weapon || !effect) return false;
    const uint8_t* fx = nf::p32_ptr<const uint8_t>(effect);
    static const float kOne[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // 0x386a60 e 0x386a70
    if (fx[0x14] == 7) {
        SQTquattom4(kOne, reinterpret_cast<const float*>(fx + 0x30), reinterpret_cast<const float*>(fx), &m[0][0]);
    } else if (fx[0x14] == 6) {
        SQTquattom4(kOne, reinterpret_cast<const float*>(fx + 0x20), reinterpret_cast<const float*>(fx), &m[0][0]);
    } else {
        std::memset(m, 0, 64);
        m[0][0] = m[1][1] = m[2][2] = 1.0f;
        std::memcpy(m[3], fx, 12);
        m[3][3] = 1.0f;
    }
    // TransformMatrixByInstancePosition(o índice +0x15, m, true): a GetTransform, a instância e a TransformToWorldSpace.
    const RSceneObj& s = *nf::p32_ptr<const RSceneObj>(scene);
    float t[4][4];
    std::memcpy(t, nf::p32_ptr<const uint8_t>(s.instance), 64);
    t[0][3] = t[1][3] = t[2][3] = 0.0f;
    t[3][3] = 1.0f;
    const uint32_t h = s.article;
    const uint32_t index = fx[0x15];
    if (h && index < nf::p32_ptr<const uint8_t>(h)[0x1c]) {
        ProcAnimContext ctx;
        ctx.states = r32(h, 8) ? nf::p32_ptr<const uint8_t>(r32(h, 8)) : nullptr;
        uint32_t raw[16];
        std::memcpy(raw, nf::p32_ptr<const uint8_t>(h) + 0x40 + size_t(index) * 0x40, 64);
        float im[4][4];
        if (GetInstanceMatrix(raw, ctx, im)) MATRIX4_mult(&im[0][0], &t[0][0], &t[0][0]);
        else ++g_stats.guards;
    }
    MATRIX4_mult(&m[0][0], &t[0][0], &m[0][0]);
    ActWeapon_TransformToWorldSpace(weapon, &m[0][0]);
    uint32_t flags;
    std::memcpy(&flags, fx + 0x10, 4);
    if (flags & 0x100) ++g_stats.guards;  // AdjustEffectPosition: o WCollisionMgr::GetWorldHeightAtPoint (não portado)
    return true;
}

// O RSceneObj::GetInstanceSystemID (0x001c9328) de uma arma.
uint32_t ActWeapon_GetInstanceSystemID(uint32_t scene, uint32_t index) {
    const uint32_t weapon = ActWeapon_FromScene(scene);
    if (!weapon) return 0;
    const uint32_t h = nf::p32_ptr<const RSceneObj>(scene)->article;
    return h ? AnimHandle_GetInstanceSystemID(h, index) : 0u;
}

// ------------------------------------------------------------------------------------------------------ o ator

namespace {

void call_matrix(uint32_t actor, float m[16], float vel[3]) {
    ActActorData& a = A(actor);
    auto it = callbacks().find(a.callback);
    if (it == callbacks().end() || !it->second) {
        ++g_stats.guards;  // uma função que o port não registrou (o PS2 a chamaria)
        for (int i = 0; i < 16; ++i) m[i] = (i % 5) == 0 ? 1.0f : 0.0f;
        vel[0] = vel[1] = vel[2] = 0.0f;
        return;
    }
    it->second(a.user, m, vel);
}

}  // namespace

// [driving] 0x00108038 ActActor::ActActor(void *usuário, char *personagem, char *arma0, char *arma1, função, MATRIX4 &,
// bool espelhado): o ActCharacter, a escala, as matrizes, o ActPoser e o ActAnimGroup vazio.
uint32_t ActActor_Construct(uint32_t user, const char* character, const char* weapon0, const char* weapon1,
                            uint32_t callback, const float* matrix, int32_t not_mirrored) {
    ActActorData* a = new ActActorData{};
    a->user = user;
    a->not_mirrored = not_mirrored;
    a->draw_flags = 3;
    a->fov_scale = 1.0f;
    a->callback = callback;
    a->controller = 0;
    a->visible = 1;
    a->unknown34 = 0;
    a->floor_check = 1;
    a->alpha_base = 0.0f;
    a->fade = 0.0f;
    a->height = 0.0f;
    a->unknown54 = 1;
    a->pov = 0;
    a->has_callback = callback != 0 ? 1 : 0;
    // O bloco das matrizes (__builtin_new(0x394)) com o WWorldPos::WWorldPos (0x00232180) no começo: tudo zerado.
    ActActorMatrices* mx = new ActActorMatrices{};
    a->matrices = nf::p32_u(mx);
    if (weapon0 && !*weapon0) weapon0 = nullptr;
    if (weapon1 && !*weapon1) weapon1 = nullptr;
    a->has_weapon[0] = weapon0 ? 1 : 0;
    a->weapon_held[0] = a->has_weapon[0];
    a->has_weapon[1] = weapon1 ? 1 : 0;
    a->weapon_held[1] = a->has_weapon[1];
    a->character = ActCharacter_Construct(a->has_callback ^ 1, character, weapon0, weapon1);
    ActCharacter_GetScaleFactors(a->character, a->scale, a->inv_scale);
    // 0x002d1128 BuildScale(MATRIX4 &, float): a diagonal com a escala, a linha 3 o vf0.
    {
        float* s = mx->scale;
        std::memset(s, 0, 64);
        s[0] = s[5] = s[10] = a->scale;
        s[15] = 1.0f;
    }
    ActActor_InitializeMatrices(nf::p32_u(a), matrix);
    ++g_stats.actors;
    // O ActSkeleton do esqueleto do personagem (o ActSkeletonDatabase), o ActPoser e o ActAnimGroup(-1, -1).
    ActManagerData* m = manager();
    const int32_t skel_index = P<ActCharacterInfoData>(nf::p32_ptr<ActCharacterData>(a->character)->info)->skeleton;
    ActSkeleton* skel = nullptr;
    if (skel_index >= 0 && skel_index < 9) skel = g_res.skeletons[skel_index].get();
    if (!skel) ++g_stats.missing_resources;  // o PS2 leria um ActSkeleton de fora da tabela
    ActPoser* p = new ActPoser(skel, m ? m->events : 0u, a->not_mirrored, a->has_callback,
                               m ? m->frame_time : 1.0f / 60.0f);
    a->poser = nf::p32_u(p);
    // 0x0010c888 ActAnimGroup::ActAnimGroup(int, int) com -1, -1 (sem animação)
    ActAnimGroup* g = new ActAnimGroup{};
    g->index = -1;
    g->bank = -1;
    a->group = nf::p32_u(g);
    return nf::p32_u(a);
}

// [driving] 0x00108358 ActActor::~ActActor
void ActActor_Destruct(uint32_t actor) {
    ActActorData* a = P<ActActorData>(actor);
    if (!a) return;
    if (a->controller) AnimController_Destruct(a->controller);
    delete P<ActAnimGroup>(a->group);
    delete P<ActPoser>(a->poser);
    ActCharacter_Destruct(a->character);
    delete P<ActActorMatrices>(a->matrices);
    delete a;
}

// [driving] 0x00107c68 ActActor::SetNewAnimation(int banco, int índice): o StandardAnimationController novo (o anterior
// sai); devolve o controlador.
uint32_t ActActor_SetNewAnimation(uint32_t actor, int32_t bank, int32_t index) {
    ActActorData& a = A(actor);
    if (a.controller) AnimController_Destruct(a.controller);
    const uint32_t c = StandardAnimationController_Construct(a.group, a.poser, bank, index, M(actor).pose_to_world);
    a.controller = c;
    ActActor_SetStartPosOri(actor);
    return c;
}

// [driving] 0x00108438 ActActor::GetBoneIndex
int32_t ActActor_GetBoneIndex(uint32_t actor, const char* name) {
    // ActPoser::GetBoneIndex (0x00112b58) -> ActSkeleton::GetBoneIndex
    return nf::p32_ptr<const ActPoser>(A(actor).poser)->GetBoneIndex(name);
}

// [driving] 0x00108460 ActActor::InitializeMatrices(MATRIX4 &)
void ActActor_InitializeMatrices(uint32_t actor, const float* m) {
    ActActorData& a = A(actor);
    ActActorMatrices& x = M(actor);
    std::memcpy(x.local, m, 64);
    x.local[12] = ee::mul(x.local[12], a.inv_scale);
    x.local[13] = ee::mul(x.local[13], a.inv_scale);
    x.local[14] = ee::mul(x.local[14], a.inv_scale);
    std::memcpy(x.weapon_bone[0], x.local, 64);
    std::memcpy(x.weapon_bone[1], x.local, 64);
    x.floor_height = 0.0f;
    const float big = ps2::ffloat(0x49742400u);  // 1000000
    x.floor_pos[0] = big;
    x.floor_pos[2] = big;
    x.floor_pos[1] = big;
    ActActor_CalculateMatrices(actor, true);
}

// [driving] 0x00108610 ActActor::Fire(float, float, float, float): o estímulo 10 na arma 0.
void ActActor_Fire(uint32_t actor) {
    ++g_stats.fires;
    ActCharacter_PlayEvent(A(actor).character, 0, 10);
}

// [driving] 0x00108638 ActActor::CalculateMatrices(bool primeira)
void ActActor_CalculateMatrices(uint32_t actor, bool first) {
    ActActorData& a = A(actor);
    ActActorMatrices& x = M(actor);
    float h = 0.0f;
    if (a.has_callback == 0) {
        if (a.floor_check != 0 && !first) {
            const float pos[3] = {x.scaled[12], x.scaled[13], x.scaled[14]};
            const float dx = ee::sub(x.floor_pos[0], pos[0]), dz = ee::sub(x.floor_pos[2], pos[2]);
            const float d = ee::add(ee::mul(dx, dx), ee::mul(dz, dz));
            if (ee::lt(ps2::ffloat(0x3d23d70bu), d)) {
                x.floor_pos[0] = pos[0];
                x.floor_pos[1] = pos[1];
                x.floor_pos[2] = pos[2];
                // WWorldPos::FindClosestFace (0x002322c8) e WWorldPos::HeightAtPoint (0x002327a0)
                if (WorldCollision* w = world()) w->find_closest_face(x.world_pos, pos, true);
                else ++g_stats.guards;
                x.floor_height = WWorldPos_HeightAtPoint(x.world_pos, pos);
                act_note(0x002327a0, actor);
            }
        }
        h = x.floor_height;
    }
    a.height = h;
    std::memcpy(x.world, x.scale, 64);
    if (a.has_callback == 0) x.world[13] = ee::add(x.world[13], h);
    std::memcpy(x.local_copy, x.local, 64);
    if (first) {
        std::memcpy(x.initial, x.local_copy, 64);
        x.pose_to_world = nf::p32_u(a.has_callback == 1 ? x.initial : x.local_copy);
    }
    std::memcpy(x.scaled, x.local_copy, 64);
    x.scaled[13] = ee::mul(x.scaled[13], a.scale);
    x.scaled[14] = ee::mul(x.scaled[14], a.scale);
    x.scaled[12] = ee::mul(x.scaled[12], a.scale);
    x.scaled[13] = ee::add(x.scaled[13], h);
    std::memcpy(x.world2, x.scaled, 64);
    for (int i = 0; i < 2; ++i) {
        if (a.has_weapon[i] != 1) continue;
        std::memcpy(x.weapon_prev[i], x.weapon[i], 64);
        std::memcpy(x.weapon[i], x.weapon_bone[i], 64);
        x.weapon[i][13] = ee::mul(x.weapon[i][13], a.scale);
        x.weapon[i][14] = ee::mul(x.weapon[i][14], a.scale);
        x.weapon[i][12] = ee::mul(x.weapon[i][12], a.scale);
        x.weapon[i][13] = ee::add(x.weapon[i][13], h);
    }
    if (a.has_callback != 0) {
        alignas(16) float mm[16];
        float vel[3];
        call_matrix(actor, mm, vel);
        MATRIX4_mult(x.world, mm, x.world);
        MATRIX4_mult(x.world2, mm, x.world2);
        if (a.has_weapon[0] == 1) MATRIX4_mult(x.weapon[0], mm, x.weapon[0]);
        if (a.has_weapon[1] == 1) MATRIX4_mult(x.weapon[1], mm, x.weapon[1]);
    }
    if (first) {
        const float pos[3] = {x.scaled[12], x.scaled[13], x.scaled[14]};
        if (WorldCollision* w = world()) w->find_closest_face(x.world_pos, pos, true);
        else ++g_stats.guards;
        act_note(0x002322c8, actor);
    }
}

// [driving] 0x00108d48 ActActor::GetActorLocalPosOri(MATRIX4 &)
void ActActor_GetActorLocalPosOri(uint32_t actor, float* out) { std::memcpy(out, M(actor).scaled, 64); }

// [driving] 0x00108e98 ActActor::GetActorWorldPosition(COORD4 &)
void ActActor_GetActorWorldPosition(uint32_t actor, float* out) { std::memcpy(out, M(actor).world2 + 12, 16); }

// [driving] 0x00108ec8 ActActor::GetWeaponPosition(MATRIX4 &, bool, int)
void ActActor_GetWeaponPosition(uint32_t actor, float* out, int32_t weapon) {
    std::memcpy(out, nf::p32_ptr<const uint8_t>(A(actor).matrices) + 0x200 + 0x40 * weapon, 64);
}

// [driving] 0x00109140 ActActor::Update
void ActActor_Update(uint32_t actor) {
    ++g_stats.updates;
    ActActorData& a = A(actor);
    // ActActor::CheckValidActor (0x00108458): nada.
    g_act.updating = actor;
    ActManagerData* m = manager();
    ActEvents_SetCurrentActor(m ? m->events : 0u, actor);
    if (ee::lt(0.0f, a.fade)) {
        ActCharacter_SetAlpha(a.character, ee::mul(ee::mul(a.fade, a.alpha_base), 0.75f));
        const float f = ee::sub(a.fade, 1.0f);
        a.fade = f;
        if (ee::le(f, 0.0f)) a.fade = -1.0f;
    }
    ActPoser& p = *nf::p32_ptr<ActPoser>(a.poser);
    p.DoSkeletonPose();
    ActActorMatrices& x = M(actor);
    for (int i = 0; i < 2; ++i)
        if (a.has_weapon[i] != 0) p.GetWeaponBonePosOri(i, x.weapon_bone[i]);
    p.GetRootBonePosOri(x.local);
    p.DoEventPose();
    if (!a.controller) {
        ++g_stats.guards;  // o PS2 chamaria a tabela virtual do controlador nulo
    } else {
        AnimController_Update(a.controller);
    }
    ActActor_CalculateMatrices(actor, false);
    for (int i = 0; i < 2; ++i) {
        if (a.has_weapon[i] == 0) continue;
        alignas(16) float w[16];
        ActActor_GetWeaponPosition(actor, w, i);
        if (a.weapon_held[i] != 0) ActCharacter_SetWeaponBone(a.character, i, w);
    }
    ActActor_SetWeaponVelocity(actor);
}

// [driving] 0x00109a08 ActActor::DropWeapon(float)
void ActActor_DropWeapon(uint32_t actor, float value) {
    ++g_stats.drops;
    ActActorData& a = A(actor);
    if (ee::lt(value, ps2::ffloat(0x3f733333u))) {
        a.weapon_held[0] = 1;
    } else {
        if (ee::lt(value, ps2::ffloat(0x3ff9999au))) {
            if (a.weapon_held[0] != 1) {
                a.weapon_held[0] = 0;
                return;
            }
            // 0x0010aa58 ActActor::SpawnWeapon: a velocidade da arma (a diferença das matrizes x 20, mais 0,6
            // da velocidade da função), o giro (VU0_m4toquat, VU0_quatstoangvel) e o Simulation::SpawnNewtonObject
            // (não portado: o Newton dos pedaços): contado.
            ++g_stats.spawn_weapon;
            act_note(0x0010aa58, actor);
        }
        a.weapon_held[0] = 0;
    }
}

// [driving] 0x0010abe0 ActActor::SetWeaponVelocity
void ActActor_SetWeaponVelocity(uint32_t actor) {
    ActActorData& a = A(actor);
    float vel[3] = {0.0f, 0.0f, 0.0f};
    if (a.callback != 0) {
        alignas(16) float mm[16];
        call_matrix(actor, mm, vel);
    }
    if (a.character) ActCharacter_SetWeaponVelocity(a.character, vel);
}

bool ActActor_IsAnimationDone(uint32_t actor) { return nf::p32_ptr<const ActPoser>(A(actor).poser)->IsAnimationDone(); }

// [driving] 0x0010acc0 ActActor::SetStartPosOri
void ActActor_SetStartPosOri(uint32_t actor) {
    // ActPoser::DoInitialPoses (0x001143b8)
    nf::p32_ptr<ActPoser>(A(actor).poser)->DoInitialPoses();
}

// [driving] 0x0010ace0 ActActor::SetTimeScale(float)
void ActActor_SetTimeScale(uint32_t actor, float scale) { nf::p32_ptr<ActPoser>(A(actor).poser)->time_scale = scale; }

// [driving] 0x0010ad90 ActActor::GetAnimationOrigin(COORD3 &)
void ActActor_GetAnimationOrigin(uint32_t actor, float* out) {
    nf::p32_ptr<const ActPoser>(A(actor).poser)->GetAnimationOrigin(out);
    const float s = A(actor).scale;
    out[0] = ee::mul(out[0], s);
    out[1] = ee::mul(out[1], s);
    out[2] = ee::mul(out[2], s);
}

// [driving] 0x0010adf8 ActActor::SetAnimationOrigin(COORD3 &)
void ActActor_SetAnimationOrigin(uint32_t actor, float* origin) {
    ActActorData& a = A(actor);
    origin[0] = ee::mul(origin[0], a.inv_scale);
    origin[1] = ee::mul(origin[1], a.inv_scale);
    origin[2] = ee::mul(origin[2], a.inv_scale);
    nf::p32_ptr<ActPoser>(a.poser)->SetAnimationOrigin(origin);
    if (a.has_callback != 0) std::memcpy(M(actor).initial + 12, origin, 12);
}

// [driving] 0x0010ad00 ActActor::ChangeAnimationOrigin(COORD3 &)
void ActActor_ChangeAnimationOrigin(uint32_t actor, float* origin) {
    ActActorData& a = A(actor);
    origin[0] = ee::mul(origin[0], a.inv_scale);
    origin[1] = ee::mul(origin[1], a.inv_scale);
    origin[2] = ee::mul(origin[2], a.inv_scale);
    nf::p32_ptr<ActPoser>(a.poser)->ChangeAnimationOrigin(origin);
    if (a.has_callback != 0) std::memcpy(M(actor).initial + 12, origin, 12);
}

namespace {

// A rotação da matriz inicial do ator com a função (o +0x340), pelo VU0_MATRIX4_mult à esquerda.
void rotate_initial(uint32_t actor, const float r[16]) {
    float* m = M(actor).initial;
    MATRIX4_mult(r, m, m);
}

}  // namespace

// [driving] 0x0010afc0 ActActor::RotateActor(float voltas)
void ActActor_RotateActor(uint32_t actor, float turns) {
    ActActorData& a = A(actor);
    nf::p32_ptr<ActPoser>(a.poser)->Rotate(turns);
    if (a.has_callback != 0) {
        const float ang = ee::mul(turns, ps2::ffloat(0x40c90fdbu));
        const float c = nf::fm::cos(ang), s = nf::fm::sin(ang);
        const float r[16] = {c, 0.0f, ee::neg(s), 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, s, 0.0f, c, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        rotate_initial(actor, r);
    }
}

// [driving] 0x0010b098 ActActor::RotateActorX(float voltas)
void ActActor_RotateActorX(uint32_t actor, float turns) {
    ActActorData& a = A(actor);
    nf::p32_ptr<ActPoser>(a.poser)->RotateX(turns);
    if (a.has_callback != 0) {
        const float ang = ee::mul(turns, ps2::ffloat(0x40c90fdbu));
        const float c = nf::fm::cos(ang), s = nf::fm::sin(ang);
        const float r[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, s, 0.0f, 0.0f, ee::neg(s), c, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        rotate_initial(actor, r);
    }
}

// [driving] 0x0010b180 ActActor::SetSuppressAnimationTranslation(bool)
void ActActor_SetSuppressAnimationTranslation(uint32_t actor, int32_t on) {
    nf::p32_ptr<ActPoser>(A(actor).poser)->suppress_translation = on;
    alignas(16) float pos[4], origin[4];
    ActActor_GetActorWorldPosition(actor, pos);
    ActActor_GetAnimationOrigin(actor, origin);
    origin[0] = pos[0];
    origin[2] = pos[2];
    ActActor_SetAnimationOrigin(actor, origin);
}

// [driving] 0x001090c0 ActActor::CreateGlobalPoseOverrides (o ActGlobalPoseOverrideArray do poser, +0x4c)
void ActActor_CreateGlobalPoseOverrides(uint32_t actor, int32_t count, const int32_t* bones, int32_t local_space) {
    ActPoser& p = *nf::p32_ptr<ActPoser>(A(actor).poser);
    ActGlobalPoseOverrideArray_CreateGlobalPoseOverrides(*nf::p32_ptr<ActGlobalPoseOverrideArrayData>(p.overrides), count,
                                                         bones, local_space);
}

// [driving] 0x001090e0 ActActor::DeleteGlobalPoseOverrides
void ActActor_DeleteGlobalPoseOverrides(uint32_t actor) {
    ActPoser& p = *nf::p32_ptr<ActPoser>(A(actor).poser);
    ActGlobalPoseOverrideArray_DeleteGlobalPoseOverrides(*nf::p32_ptr<ActGlobalPoseOverrideArrayData>(p.overrides));
}

// [driving] 0x00109100 ActActor::SetGlobalPoseOverride
void ActActor_SetGlobalPoseOverride(uint32_t actor, int32_t index, const float* m, float weight) {
    ActPoser& p = *nf::p32_ptr<ActPoser>(A(actor).poser);
    ActGlobalPoseOverrideArray_SetGlobalPoseOverride(*nf::p32_ptr<ActGlobalPoseOverrideArrayData>(p.overrides), index, m,
                                                     weight);
}

// [driving] 0x00109120 ActActor::SetGlobalPoseOverrides
void ActActor_SetGlobalPoseOverrides(uint32_t actor, int32_t /*count*/, const float* m, const float* weights) {
    ActPoser& p = *nf::p32_ptr<ActPoser>(A(actor).poser);
    ActGlobalPoseOverrideArray_SetGlobalPoseOverrides(*nf::p32_ptr<ActGlobalPoseOverrideArrayData>(p.overrides), m,
                                                      weights);
}

// -------------------------------------------------------------------------------------------- a lista dos atores

// [driving] 0x0010b540 ActActorDatabase::ActActorDatabase
void ActActorDatabase_Construct() {
    if (g_act.database) return;
    ActListNode* sentinel = new ActListNode{};
    sentinel->next = sentinel->prev = nf::p32_u(sentinel);
    uint32_t* db = new uint32_t[1]{nf::p32_u(sentinel)};
    g_act.database = nf::p32_u(db);
}

namespace {
ActListNode* sentinel() {
    return g_act.database ? nf::p32_ptr<ActListNode>(*nf::p32_ptr<const uint32_t>(g_act.database)) : nullptr;
}
}  // namespace

std::vector<uint32_t> act_actor_list() {
    std::vector<uint32_t> out;
    ActListNode* s = sentinel();
    if (!s) return out;
    for (uint32_t n = s->next; n != nf::p32_u(s); n = nf::p32_ptr<const ActListNode>(n)->next)
        out.push_back(nf::p32_ptr<const ActListNode>(n)->actor);
    return out;
}

// [driving] 0x0010b3b8 ActActorDatabase::GetNewActorHandle(void *, char *, char *, char *, função, MATRIX4 &, bool):
// o ator novo num nó no começo da lista; devolve o handle (o nó).
uint32_t ActActorDatabase_GetNewActorHandle(uint32_t user, const char* character, const char* weapon0,
                                            const char* weapon1, uint32_t callback, const float* matrix,
                                            int32_t mirrored) {
    // (o FastAlloc(100) e o ActActor com o último argumento ^ 1)
    const uint32_t a = ActActor_Construct(user, character, weapon0, weapon1, callback, matrix, mirrored ^ 1);
    ActActorDatabase_Construct();
    ActListNode* s = sentinel();
    // O nó novo antes do primeiro (o insert(begin())); o handle é o primeiro da lista, o novo.
    ActListNode* first = nf::p32_ptr<ActListNode>(s->next);
    ActListNode* node = new ActListNode{};
    node->actor = a;
    node->next = nf::p32_u(first);
    node->prev = first->prev;
    nf::p32_ptr<ActListNode>(first->prev)->next = nf::p32_u(node);
    first->prev = nf::p32_u(node);
    return s->next;
}

// [driving] 0x0010b4e0 ActActorDatabase::KillActorByHandle (o ator e o nó)
void ActActorDatabase_KillActorByHandle(uint32_t handle) {
    ActListNode* node = P<ActListNode>(handle);
    if (!node) {
        ++g_stats.guards;  // o PS2 leria o nó nulo
        return;
    }
    if (node->actor) {
        if (g_act.current == node->actor) g_act.current = 0;
        if (g_act.updating == node->actor) g_act.updating = 0;
        ActActor_Destruct(node->actor);
    }
    nf::p32_ptr<ActListNode>(node->prev)->next = node->next;
    nf::p32_ptr<ActListNode>(node->next)->prev = node->prev;
    delete node;
}

// [driving] 0x0010b800 ActActorDatabase::UpdateAll
void ActActorDatabase_UpdateAll() {
    // O 0x339f44 (o estado da Simulation): 3, parada.
    if (simulation().state == 3) return;
    ActListNode* s = sentinel();
    if (!s) return;
    for (uint32_t n = s->next; n != nf::p32_u(s); n = nf::p32_ptr<const ActListNode>(n)->next)
        ActActor_Update(nf::p32_ptr<const ActListNode>(n)->actor);
}

// [driving] 0x0010b260 ActActorDatabase::ShutDown (com o ~ActActorDatabase 0x0010b588: o clear da lista)
void ActActorDatabase_ShutDown() {
    ActListNode* s = sentinel();
    if (s) {
        uint32_t n = s->next;
        while (n != nf::p32_u(s)) {
            ActListNode* node = nf::p32_ptr<ActListNode>(n);
            n = node->next;
            if (node->actor) ActActor_Destruct(node->actor);
            delete node;
        }
        delete s;
        delete[] nf::p32_ptr<uint32_t>(g_act.database);
    }
    g_act.database = 0;
    g_act.current = g_act.updating = 0;
}

// --------------------------------------------------------------------------------------------------- o desenho

void act_draw_list(std::vector<ActDrawItem>& out) {
    out.clear();
    for (uint32_t actor : act_actor_list()) {
        const ActActorData& a = A(actor);
        // ActActor::Draw (0x00109500): visível, o corpo (o bit 0 do +0x58) depois do Skin, as armas (DrawWeapons
        // 0x00109918: mostradas com o bit 1 e na mão).
        if (a.visible == 0) continue;
        ++g_stats.draws;
        const ActCharacterData& c = *nf::p32_ptr<const ActCharacterData>(a.character);
        const ActPoser& p = *nf::p32_ptr<const ActPoser>(a.poser);
        ActDrawItem it;
        it.actor = actor;
        it.model = P<CarModel>(c.model);
        std::memcpy(it.world, M(actor).world, 64);
        it.pov = a.pov;
        it.fov_scale = a.fov_scale;
        it.alpha = P<ActRenderOptionsData>(c.render_options)->alpha;
        if (a.draw_flags & 1) {
            const int n = Skeleton_NumBones(p.skel().skeleton);
            it.skin.assign(size_t(n) * kMatrixFloats, 0.0f);
            p.Skin(it.skin.data());
        }
        for (int i = 0; i < 2; ++i) {
            if (!c.weapon[i] || a.weapon_held[i] != 1 || !(a.draw_flags & 2)) continue;
            const ActWeaponData& w = *nf::p32_ptr<const ActWeaponData>(c.weapon[i]);
            const ActWeaponResource* r = P<ActWeaponResource>(w.aux);
            ActDrawItem::Weapon dw;
            dw.model = r ? r->model.get() : nullptr;
            const uint32_t scene = nf::p32_u(&w.scene);
            for (uint32_t k = 0; k < (r ? r->count : 0); ++k) {
                float m[4][4];
                ActWeapon_GetInstancePosition(scene, int32_t(k), m);
                dw.instances.push_back(std::vector<float>(&m[0][0], &m[0][0] + 16));
            }
            it.weapons.push_back(std::move(dw));
        }
        out.push_back(std::move(it));
    }
}

}  // namespace drv::anim
