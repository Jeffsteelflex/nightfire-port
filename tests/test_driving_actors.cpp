// Os atores e os personagens das missões de carro (driving/anim/act_actor.h, driving/game/ai_character.h): propriedades
// lidas no código do jogo, conferidas sem o original (a conferência bit a bit contra o DRIVING.ELF fica no
// nf_driving_check, parte 20, `--so integracao.atores`): os eventos de um canal (o cursor, o laço), os nomes dos eventos,
// os valores do construtor, as zonas do corpo, a máquina de estados das mãos com atores falsos, o dano do Human e a
// lista dos atores.

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "driving/anim/act_actor.h"
#include "driving/anim/eagl_anim.h"
#include "driving/game/ai_character.h"
#include "driving/physics/simple_body.h"
#include "driving/physics/simulation.h"
#include "platform/ptr32.h"
#include "test_framework.h"

namespace an = drv::anim;

namespace {

// Os tratadores chamados pelo RawEventChannel_Eval (o número do evento e o tempo).
std::vector<std::pair<uint32_t, float>> g_fired;
uint32_t g_events_base = 0;

void record_event(uint32_t, float time, uint32_t event, uint32_t) {
    g_fired.push_back({(event - g_events_base) / 0x10u, time});
}

// Um canal de eventos com o leiaute do jogo: +4 a contagem, +8 os eventos (0x10 bytes: o número, o quadro, dois valores).
struct EventData {
    std::vector<uint32_t> w;
    explicit EventData(const std::vector<float>& times) : w(2 + 4 * times.size() + 4, 0) {
        w[1] = uint32_t(times.size());
        for (size_t i = 0; i < times.size(); ++i) {
            w[2 + 4 * i] = 0;  // o tratador 0
            std::memcpy(&w[2 + 4 * i + 1], &times[i], 4);
        }
    }
    uint32_t p32() { return nf::p32_u(w.data()); }
};

// Os atores falsos das mãos (o nó com o ator em +8; só o visível é lido) e o que as mãos pedem ao ator.
struct FakeActors {
    an::ActActorData actors[8];
    an::ActListNode nodes[8];
    FakeActors() {
        for (int i = 0; i < 8; ++i) {
            actors[i] = an::ActActorData{};
            nodes[i] = an::ActListNode{};
            nodes[i].actor = nf::p32_u(&actors[i]);
        }
    }
    uint32_t node(int i) { return nf::p32_u(&nodes[i]); }
};
std::vector<std::pair<int32_t, int32_t>> g_anims;  // (banco, animação) de cada SetNewAnimation
bool g_done = false;

struct ApiScope {
    drv::AICharacterActorApi saved = drv::ai_character_actor_api();
    drv::HandsGlobals saved_hands = drv::hands_globals();
    ApiScope() {
        g_anims.clear();
        drv::AICharacterActorApi& a = drv::ai_character_actor_api();
        a.set_new_animation = [](uint32_t, int32_t bank, int32_t index) -> uint32_t {
            g_anims.push_back({bank, index});
            return 0x1000u + uint32_t(g_anims.size());
        };
        a.is_animation_done = [](uint32_t) { return g_done; };
        a.current_frame = [](uint32_t) { return 0.0f; };
        a.current_time = [](uint32_t) { return 0.5f; };
        a.total_time = [](uint32_t) { return 2.0f; };
        a.remaining_time = [](uint32_t) { return 1.5f; };
    }
    ~ApiScope() {
        drv::ai_character_actor_api() = saved;
        drv::hands_globals() = saved_hands;
    }
};

}  // namespace

TEST(driving_actor_raw_events_forward_and_loop) {
    // Três eventos nos quadros 1, 2 e 4; os tratadores pela tabela (o 0).
    EventData d({1.0f, 2.0f, 4.0f});
    g_events_base = d.p32() + 8;
    uint32_t handler_obj[2] = {0, 0};
    uint32_t handlers[1] = {nf::p32_u(handler_obj)};
    an::RawEventDispatch saved = an::raw_event_dispatch();
    an::raw_event_dispatch() = record_event;
    int32_t cursor = 0;
    float last = 0.0f;
    // De 0 a 2,5: os dois primeiros.
    g_fired.clear();
    an::RawEventChannel_Eval(d.p32(), cursor, last, 0.0f, 2.5f, nf::p32_u(handlers), 0);
    CHECK(g_fired.size() == 2 && g_fired[0].first == 0 && g_fired[1].first == 1);
    CHECK(cursor == 2 && last == 2.5f);
    // De 2,5 a 5: o terceiro; o cursor fica no último.
    g_fired.clear();
    an::RawEventChannel_Eval(d.p32(), cursor, last, 2.5f, 5.0f, nf::p32_u(handlers), 0);
    CHECK(g_fired.size() == 1 && g_fired[0].first == 2 && cursor == 2);
    // O laço (o tempo volta de 5 para 1,5): o resto depois do cursor (nada: o 4 já foi) e o começo até 1,5.
    g_fired.clear();
    an::RawEventChannel_Eval(d.p32(), cursor, last, 5.0f, 1.5f, nf::p32_u(handlers), 0);
    CHECK(g_fired.size() == 1 && g_fired[0].first == 0 && cursor == 1);
    // O laço com um evento ainda não passado (de 3 volta a 0,5): o do quadro 4 e depois nenhum do começo.
    g_fired.clear();
    cursor = 2;
    last = 3.0f;
    an::RawEventChannel_Eval(d.p32(), cursor, last, 3.0f, 0.5f, nf::p32_u(handlers), 0);
    CHECK(g_fired.size() == 1 && g_fired[0].first == 2 && cursor == 0);
    // O mesmo quadro (anterior == tempo): só os eventos dele.
    g_fired.clear();
    cursor = 0;
    last = 0.0f;
    an::RawEventChannel_Eval(d.p32(), cursor, last, 2.0f, 2.0f, nf::p32_u(handlers), 0);
    CHECK(g_fired.size() == 1 && g_fired[0].first == 1);
    // Sem tratador para o número: nada é chamado.
    handlers[0] = 0;
    g_fired.clear();
    cursor = 0;
    last = 0.0f;
    an::RawEventChannel_Eval(d.p32(), cursor, last, 0.0f, 9.0f, nf::p32_u(handlers), 0);
    CHECK(g_fired.empty() && cursor == 2);
    an::raw_event_dispatch() = saved;
}

TEST(driving_actor_event_names_sorted_and_numbered) {
    const uint32_t t = an::ActEventResolver_Construct();
    int32_t id = -2;
    CHECK(!an::EventTarget_ResolveEventId(t, "Character_X", id));  // sem o "event."
    CHECK(an::EventTarget_ResolveEventId(t, "event.zeta", id) && id == 0);
    CHECK(an::EventTarget_ResolveEventId(t, "event.alpha", id) && id == 1);
    CHECK(an::EventTarget_ResolveEventId(t, "event.zeta", id) && id == 0);  // o mesmo nome, o mesmo número
    // Os números na ordem em que apareceram; os nomes em ordem (a busca binária).
    char name[32];
    for (int i = 0; i < 120; ++i) {
        std::snprintf(name, sizeof name, "event.n%03d", (i * 37) % 120);
        CHECK(an::EventTarget_ResolveEventId(t, name, id));
    }
    const an::EventTargetData& d = *nf::p32_ptr<const an::EventTargetData>(t);
    CHECK(d.count == 122 && d.capacity >= 122);
    std::string prev;
    for (int i = 0; i < d.count; ++i) {
        const std::string s = nf::p32_ptr<const char>(*nf::p32_ptr<const uint32_t>(d.names + 8u * uint32_t(i)));
        CHECK(prev < s);
        prev = s;
    }
    int32_t pos = 0;
    CHECK(an::EventTarget_GetEventId(t, "alpha", pos) == 1 && an::EventTarget_GetEventId(t, "nope", pos) == -1);
}

TEST(driving_aicharacter_constructor_and_zones) {
    drv::AICharacterData c;
    std::memset(&c, 0xcd, sizeof c);
    drv::AICharacter_Construct(c);
    CHECK(c.vtable == drv::kVtAICharacter && c.range_sq == 5625.0f && c.clip == 15 && c.clip_size == 15);
    CHECK(c.counts_hits == 1 && c.unknown8c == 60 && c.actor == 0 && c.matrix[0] == 1.0f && c.body[15] == 1.0f);
    CHECK(std::fabs(c.offset[0] + 0.08f) < 1e-6f && std::fabs(c.offset[2] - 0.12f) < 1e-6f);
    // O que o construtor não escreve fica (o +0xa8, o +0x70).
    uint32_t w;
    std::memcpy(&w, &c.zone_last_hit, 4);
    CHECK(w == 0xcdcdcdcdu);
    // A escala das zonas (0 a zona 0, 6 a cabeça, 1 as outras).
    const drv::AIVtable* v = drv::ai_vtable(drv::kVtAICharacter);
    CHECK(v && v->zone_hit_point_scale(0, 10.0f, 0) == 0.0f && v->zone_hit_point_scale(0, 10.0f, 1) == 6.0f &&
          v->zone_hit_point_scale(0, 10.0f, 3) == 1.0f);
    // O polo: o y de cima dá profundidade 0, o de baixo 1; o mapa de bytes com sinal.
    float d = -1, l = -1;
    const float up[3] = {0, 1, 0}, down[3] = {0, -1, 0};
    drv::Util_VecToPoleDepthAndLong(up, d, l);
    CHECK(d == 0.0f && l >= 0.0f && l <= 1.0f);
    drv::Util_VecToPoleDepthAndLong(down, d, l);
    CHECK(d == 1.0f);
    int8_t map[4] = {1, -2, 3, -4};
    CHECK(drv::Util_CharMapLookup(1.0f, 1.0f, nf::p32_u(map), 2, 2) == -4);
    CHECK(drv::Util_CharMapLookup(0.0f, 0.0f, nf::p32_u(map), 2, 2) == 1);
    CHECK(drv::Util_CharMapLookup(1.0f, 0.0f, nf::p32_u(map), 2, 2) == -2);
}

TEST(driving_hands_fire_reload_and_change) {
    ApiScope scope;
    FakeActors fa;
    drv::HandsGlobals& g = drv::hands_globals();
    for (int i = 0; i < 8; ++i) {
        g.actor[i] = fa.node(i);
        g.toggle[i] = 0;
    }
    auto h = std::make_unique<drv::AICharacterHandsData>();
    drv::AICharacterHands_Construct(*h, 3);  // o fuzil (o banco 0xe)
    const uint32_t self = nf::p32_u(h.get());
    CHECK(h->base.bank == 0xe && h->hide_on_change == 1 && h->base.vtable == drv::kVtAICharacterHands);
    h->base.actor = fa.node(3);
    // Parado (estado 2): pode atirar; o tiro põe o estado 11, a animação de tiro do lado e troca o lado.
    h->base.state = 2;
    CHECK(drv::AICharacterHands_AbleToFire(self));
    drv::AICharacterHands_Fire(self);
    CHECK(h->base.state == 0xb && g.toggle[3] == 1 && g_anims.size() == 1 && g_anims[0] == std::make_pair(0xe, 0));
    CHECK(h->base.controller == 0x1001u);
    CHECK(!drv::AICharacterHands_AbleToFire(self));
    // Atirando, a recarga fica pedida; parado de novo, recarrega (o estado 12, a animação 5).
    drv::AICharacterHands_Reload(self);
    CHECK(h->reload_pending == 1 && h->base.state == 0xb);
    h->base.state = 2;
    CHECK(!drv::AICharacterHands_AbleToFire(self));  // com a recarga pedida
    drv::AICharacterHands_Reload(self);
    CHECK(h->base.state == 0xc && h->reload_pending == 0 && g_anims.back() == std::make_pair(0xe, 5));
    // A troca de arma com o braço ocupado fica pendente; o fuzil desarma (animação 3) antes de trocar.
    drv::AICharacterHands_ChangeWeapon(self, 1);
    CHECK(h->pending_weapon == 1);
    h->base.state = 2;
    drv::AICharacterHands_ChangeWeapon(self, 1);
    CHECK(h->base.state == 1 && h->base.sub_state == 0xd && h->base.index == 1 && g_anims.back() == std::make_pair(0xe, 3));
    // O DoNeutral espera o desarme (DoUnarming com a animação acabada) e arma a nova: o ator da arma 1.
    g_done = true;
    h->base.state = 0;  // o DoUnarming já rodou: o estado 0
    h->base.sub_state = 0xd;
    h->visible = 1;
    drv::ai_vtable(drv::kVtAICharacterHands)->states[1](self);
    CHECK(h->base.state == 1 && h->base.sub_state == 8 && h->base.actor == fa.node(1) && fa.actors[1].visible == 1);
    CHECK(fa.actors[3].visible == 0 && h->base.bank == 0xc && g_anims.back() == std::make_pair(0xc, 1));
    g_done = false;
}

TEST(driving_human_damage_rules) {
    ApiScope scope;
    drv::simulation_reset(60);
    drv::Simulation& sim = drv::simulation();
    FakeActors fa;
    auto c = std::make_unique<drv::AICharacterData>();
    drv::AICharacter_Construct(*c);
    c->actor = fa.node(2);
    c->counts_hits = 0;
    fa.actors[2].visible = 1;
    float hp = 30.0f;
    auto hm = std::make_unique<drv::Human>();
    hm->po.type = 4;
    hm->po.slot = int16_t(drv::AssignSimpleBodySlot(sim, hm.get()));
    hm->po.hit_point_loc = nf::p32_u(&hp);
    hm->h.character = nf::p32_u(c.get());
    drv::SimpleRigidBody* b = drv::GetSimpleRigidBody(sim, hm->po.slot);
    b->flags = 0;
    const float from[3] = {0, 0, 0}, to[3] = {1, 0, 0};
    // O tiro de bala (tipo 3) abaixo de 20 não conta; o de 25 tira a vida e marca o ponto.
    CHECK(drv::Human_ApplyDamage(*hm, 10.0f, from, to, 3, 0) == 0 && hp == 30.0f);
    CHECK(drv::Human_ApplyDamage(*hm, 25.0f, from, to, 3, 0) == 0x80 && hp == 5.0f && hm->h.dead == 0);
    CHECK(hm->h.hit_pos[0] == 1.0f);
    // A vida no fim: morto e o corpo sem colisão (Deactivate).
    CHECK(drv::Human_ApplyDamage(*hm, 25.0f, from, to, 2, 0) == 0x80 && hp == 0.0f && hm->h.dead == 1);
    CHECK((b->flags & 1) != 0);
    CHECK(drv::Human_ApplyDamage(*hm, 25.0f, from, to, 2, 0) == 0);  // morto não leva mais
    // Um ator escondido não leva dano; o Human de tipo 1 também não.
    hm->h.dead = 0;
    hp = 50.0f;
    fa.actors[2].visible = 0;
    CHECK(drv::Human_ApplyDamage(*hm, 25.0f, from, to, 2, 0) == 0 && hp == 50.0f);
    fa.actors[2].visible = 1;
    hm->h.kind = 1;
    CHECK(drv::Human_ApplyDamage(*hm, 25.0f, from, to, 2, 0) == 0);
    hm->h.character = 0;
    drv::ReleaseSimpleBodySlot(sim, hm->po.slot);
    hm->po.slot = -1;
}

TEST(driving_actor_list_front_insert_and_kill) {
    an::act_reset();
    an::ActActorDatabase_Construct();
    const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    // Sem recursos (esqueletos, modelos): os atores existem, contados como faltando.
    const uint32_t a = an::ActActorDatabase_GetNewActorHandle(0, "6.a_snipe.b_snipe.70", nullptr, nullptr, 0, m, 0);
    const uint32_t b = an::ActActorDatabase_GetNewActorHandle(0, "2.h_test.h_test.t_test.100", nullptr, nullptr, 0, m, 1);
    std::vector<uint32_t> list = an::act_actor_list();
    CHECK(list.size() == 2 && list[0] == an::act_actor_of(b) && list[1] == an::act_actor_of(a));
    // O GetNewActorHandle passa o último argumento invertido (o +0x44 do ator).
    CHECK(nf::p32_ptr<const an::ActActorData>(an::act_actor_of(a))->not_mirrored == 1);
    CHECK(nf::p32_ptr<const an::ActActorData>(an::act_actor_of(b))->not_mirrored == 0);
    CHECK(an::act_stats().missing_resources > 0);
    an::ActActorDatabase_KillActorByHandle(b);
    list = an::act_actor_list();
    CHECK(list.size() == 1 && list[0] == an::act_actor_of(a));
    an::act_reset();
    CHECK(an::act_actor_list().empty());
}
