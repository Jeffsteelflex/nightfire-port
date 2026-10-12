// Testes das falas dos convidados de 07000007 ao jogador (NDrone2_BondTalk e DroneFunc_DoBondTalk, em
// engine/drone_behaviour.cpp) sem dados do jogo: as condições, a fala escolhida por classe e pele, as esperas e a
// conversa de seis falas da classe 0x12. A fala pedida é observada pelo quadro que NDrone2_PlaySFX grava na entrada de
// Drone_SfxInfo (last), e o fim de uma fala é simulado zerando Drone_tag::sound.

#include <cstring>
#include <vector>

#include "engine/anim.h"
#include "engine/drone.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/npc.h"
#include "engine/object.h"
#include "engine/resetmap.h"
#include "engine/sound.h"
#include "platform/psi.h"
#include "platform/psi_sound.h"
#include "test_framework.h"

namespace {

alignas(16) Drone_tag g_drone;
alignas(16) obj_tag g_obj;
alignas(16) ANIMOBJ_tag g_anim;
alignas(16) obj_tag g_player;

constexpr uint32_t kNone = 0xffffffffu;
constexpr uint32_t kStart = 1000;

// As falas com rosto apontam para scripts que não estão na tabela: NDrone2_SpeechSFX então toca só o som (e zera o
// script da entrada, por isso isto vai antes de cada tick). Com o script zerado e need_face, a fala ficaria muda. As
// 0x54 e 0x56, pedidas sem rosto, não têm script (como no disco): sem rosto, uma entrada com script não toca.
void unloaded_faces() {
    for (uint32_t i = 0x52; i <= 0x9a; ++i) Drone_SfxInfo[i].script = (i == 0x54 || i == 0x56) ? 0 : 0x6ff0000u + i;
}

// A fase 07000007, o jogador, as falas 0x52-0x9a com som, sem intervalo.
void fresh() {
    psiMemHeapSize = 1u << 21;
    PtrHeap = nullptr;
    Mem_Init();
    hashtable_initialise();  // vazia: os scripts de rosto das falas não estão nela
    psi_sound_debug_power_on();
    std::memset(&GameState, 0, sizeof(GameState));
    GameState.level_file = 0x7000007;
    GameState.difficulty = 2;
    GameState.frame = kStart;
    std::memset(MPSettings, 0, sizeof(MPSettings));
    for (obj_tag*& p : glb_players) p = nullptr;
    Drone_bDisableSystem = 0;
    Drone_LevelReset();
    LList_Init(Drone_List(), 0xd20, 0);
    NPCGlobals.postload_frame = 0;
    NPCGlobals.dummy_player_unknown = 0;
    std::memset(Drone_SfxInfo, 0, sizeof(Drone_SfxInfo));
    for (uint32_t i = 0x52; i <= 0x9a; ++i) {
        Drone_SfxInfo[i].sfx = 0x100 + i;
        Drone_SfxInfo[i].volume = 100.0f;
        Drone_SfxInfo[i].range_a = 15.0f;
        Drone_SfxInfo[i].range_b = 10.0f;
    }
    unloaded_faces();
    FRAME_RATE_DIV = 1.0f;
    FRAME_RATE_MUL = 1.0f;
    FRAME_RATE_INT = 60;
    REC_FRAME_RATE = 1.0f / 60.0f;
    std::memset(&g_player, 0, sizeof(g_player));
    glb_players[0] = &g_player;
    sound_debug_power_on();
    Sound_Init();
}

// Um convidado desarmado, desenhado há pouco, a 2 m do jogador e virado para ele, no começo do estado (state_a 0xff),
// com o jogador mirando nele.
Drone_tag* make_guest(uint16_t klass, uint32_t skin) {
    std::memset(&g_drone, 0, sizeof(g_drone));
    std::memset(&g_obj, 0, sizeof(g_obj));
    std::memset(&g_anim, 0, sizeof(g_anim));
    g_obj.type = 2;
    g_obj.data = &g_drone;
    g_obj.state = 5;
    g_obj.unknown_fc = 2;
    g_obj.sphere_src = reinterpret_cast<uint8_t*>(&g_anim);
    g_drone.obj = &g_obj;
    g_drone.flags = 0x100;
    g_drone.health = 10.0f;
    g_drone.behaviour = g_drone.behaviour_a;
    g_drone.klass = klass;
    g_drone.skin = skin;
    g_drone.opp_dist = 2.0f;
    g_drone.opp_bearing = 0.1f;
    g_drone.in_sights = 1;
    g_drone.state_a = 0xff;
    LList_Add(Drone_List(), &g_drone.node);
    Drone_SM_InitObject(&g_obj);
    return &g_drone;
}

DCVars_tag dcv_of(Drone_tag* d) {
    DCVars_tag dcv{};
    Drone_DCVfromOBJ(d->obj, &dcv);
    return dcv;
}

// A fala pedida neste quadro (a entrada de Drone_SfxInfo cujo last é o quadro de agora), ou kNone.
uint32_t line_now() {
    uint32_t found = kNone;
    for (uint32_t i = 0; i < kDroneSfxCount; ++i) {
        if (Drone_SfxInfo[i].sfx != 0 && Drone_SfxInfo[i].last == GameState.frame) found = i;
    }
    return found;
}

// Avança os quadros e chama NDrone2_BondTalk uma vez (como o tick do estado); a fala anterior terminou.
uint32_t talk_after(Drone_tag* d, uint32_t ticks) {
    GameState.frame += ticks;
    d->sound = 0;
    unloaded_faces();
    DCVars_tag dcv = dcv_of(d);
    NDrone2_BondTalk(&dcv);
    return line_now();
}

}  // namespace

TEST(bond_talk_needs_07000007_and_a_guest_near_facing_and_unarmed) {
    struct Case {
        const char* what;
        void (*spoil)(Drone_tag*);
    };
    const Case cases[] = {
        {"outra fase", [](Drone_tag*) { GameState.level_file = 0x7000008; }},
        {"longe", [](Drone_tag* d) { d->opp_dist = 3.5f; }},
        {"de costas", [](Drone_tag* d) { d->opp_bearing = -2.0f; }},
        {"não desenhado", [](Drone_tag* d) { d->obj->unknown_fc = 0; }},
        {"armado", [](Drone_tag*) { g_anim.weapon = 2; }},
        {"sem jogador", [](Drone_tag*) { glb_players[0] = nullptr; }},
    };
    for (const Case& c : cases) {
        fresh();
        Drone_tag* d = make_guest(0x11, 0x5000012);
        c.spoil(d);
        DCVars_tag dcv = dcv_of(d);
        CHECK(NDrone2_BondTalk(&dcv) == 0);
        CHECK(d->state_a == 0xff);  // nada mudou
        CHECK(talk_after(d, 120) == kNone);
    }
    // No limite (3 m, 90 graus) fala; uma classe sem fala só devolve 1.
    fresh();
    Drone_tag* d = make_guest(0x11, 0x5000012);
    d->opp_dist = 3.0f;
    d->opp_bearing = -1.5707f;
    talk_after(d, 0);
    CHECK(talk_after(d, 61) == 0x8e);
    fresh();
    d = make_guest(3, 0x5000004);
    DCVars_tag dcv = dcv_of(d);
    CHECK(NDrone2_BondTalk(&dcv) == 1 && talk_after(d, 600) == kNone);
}

TEST(bond_talk_party_girls_speak_in_the_voice_of_their_skin) {
    // Classe 0xd: 0x58 + 4 x (0 a 10) + voz, a voz pela pele (0x5000020 -> 0, 22 -> 1, 23 -> 2, as outras 3).
    const uint32_t skins[] = {0x5000020, 0x5000022, 0x5000023, 0x5000015, 0x5000024};
    const uint32_t voices[] = {0, 1, 2, 3, 3};
    std::vector<bool> seen(11, false);
    for (int k = 0; k < 5; ++k) {
        fresh();
        Drone_tag* d = make_guest(0xd, skins[k]);
        // Começo do estado com a mira: sorteia state_a (0-6) e a primeira fala até 1 s depois, sem falar.
        CHECK(talk_after(d, 0) == kNone);
        CHECK(d->state_a <= 6 && kStart <= d->state_b && d->state_b < kStart + 60);
        for (int n = 0; n < 12; ++n) {
            const uint32_t at = GameState.frame;
            const uint32_t line = talk_after(d, d->state_b - at + 1);
            CHECK(line != kNone && 0x58 <= line && line <= 0x83);
            CHECK((line - 0x58) % 4 == voices[k]);
            seen[(line - 0x58) / 4] = true;
            CHECK(d->state_b == GameState.frame + 600);  // 10 s até a próxima
            CHECK(d->state_a <= 6);                      // o limite de 6 nunca corta
            // Antes da espera, nada.
            CHECK(talk_after(d, 599) == kNone);
        }
    }
    int variety = 0;
    for (bool s : seen) variety += s ? 1 : 0;
    CHECK(variety >= 5);  // as 11 falas são sorteadas
    // Sem a mira, não falam.
    fresh();
    Drone_tag* d = make_guest(0xd, 0x5000020);
    d->in_sights = 0;
    CHECK(talk_after(d, 0) == kNone && d->state_a == 0xff && talk_after(d, 700) == kNone);
}

TEST(bond_talk_classes_9_0x10_0x11_lines_and_repeats) {
    // 9: 0x95 ou 0x96 uma vez (state_a 1 depois).
    fresh();
    Drone_tag* d = make_guest(9, 0x500001b);
    CHECK(talk_after(d, 0) == kNone && d->state_a == 0 && d->state_b == 0);
    uint32_t line = talk_after(d, 1);
    CHECK(line == 0x95 || line == 0x96);
    CHECK(d->state_a == 1 && d->state_b == GameState.frame + 480);
    CHECK(talk_after(d, 481) == kNone && talk_after(d, 5000) == kNone);
    // 0x10: uma de 0x97-0x9a a cada 8 s enquanto mira.
    fresh();
    d = make_guest(0x10, 0x5000026);
    talk_after(d, 0);
    for (int n = 0; n < 6; ++n) {
        line = talk_after(d, n == 0 ? 1 : 481);
        CHECK(0x97 <= line && line <= 0x9a);
        CHECK(d->state_a == 0 && d->state_b == GameState.frame + 480);
    }
    // 0x11: 0x8e, a primeira até 1 s depois da mira, depois a cada 8 s.
    fresh();
    d = make_guest(0x11, 0x5000013);
    CHECK(talk_after(d, 0) == kNone && d->state_a == 0 && d->state_b < kStart + 60);
    CHECK(talk_after(d, 60) == 0x8e && d->state_b == GameState.frame + 480);
    CHECK(talk_after(d, 480) == kNone && talk_after(d, 1) == 0x8e);
    // Enquanto a fala anterior toca (Drone_tag::sound), não fala.
    GameState.frame += 481;
    d->sound = Sound_Play(1, 100.0f, 0, 0);
    DCVars_tag dcv = dcv_of(d);
    CHECK(NDrone2_BondTalk(&dcv) == 1 && line_now() == kNone);
    // Uma fala recusada (o intervalo da entrada) sem rosto não marca a espera: tenta de novo no próximo tick.
    fresh();
    d = make_guest(0x11, 0x5000013);
    talk_after(d, 0);
    Drone_SfxInfo[0x8e].interval = 900;
    Drone_SfxInfo[0x8e].last = kStart;
    const uint32_t before = d->state_b;
    CHECK(talk_after(d, 61) == kNone && d->state_b == before);
}

TEST(bond_talk_0xe_once_per_aim_through_do_bond_talk) {
    // Classe 0xe: DroneFunc_DoBondTalk(0x84 + 0-8, olhar 200, espera 480, sem repetir): uma fala por mira.
    fresh();
    Drone_tag* d = make_guest(0xe, 0x5000019);
    uint32_t line = talk_after(d, 0);  // começo: zera tudo e já fala (state_b 0)
    CHECK(0x84 <= line && line <= 0x8c);
    CHECK(d->state_a == 1 && d->state_c == 1 && d->state_b == GameState.frame + 480);
    CHECK(talk_after(d, 481) == kNone && talk_after(d, 3000) == kNone);  // a mesma mira: só uma
    // O jogador desvia a mira: state_a e state_c zeram; mirando de novo, fala de novo (a espera já passou).
    d->in_sights = 0;
    CHECK(talk_after(d, 1) == kNone && d->state_a == 0 && d->state_c == 0);
    d->in_sights = 1;
    line = talk_after(d, 1);
    CHECK(0x84 <= line && line <= 0x8c && d->state_c == 1);
    // Com repeat, a mesma mira fala a cada espera (DroneFunc_DoBondTalk direto).
    fresh();
    d = make_guest(0xe, 0x5000019);
    DCVars_tag dcv = dcv_of(d);
    CHECK(DroneFunc_DoBondTalk(&dcv, 0x60, 0, 100, 1) == 1 && line_now() == 0x60);
    GameState.frame += 101;
    d->sound = 0;
    unloaded_faces();
    CHECK(DroneFunc_DoBondTalk(&dcv, 0x61, 0, 100, 1) == 1 && line_now() == 0x61 && d->state_c == 2);
}

TEST(bond_talk_0x12_plays_the_six_line_conversation_in_order) {
    // Classe 0x12 (0x5000017/18): sem precisar da mira, 2 s depois do começo, as falas 0x52-0x57 com as esperas de
    // 16, 2.5, 8, 4, 4 e 5 s; depois solta a cabeça e acabou (devolve 0).
    fresh();
    Drone_tag* d = make_guest(0x12, 0x5000017);
    d->in_sights = 0;
    CHECK(talk_after(d, 0) == kNone && d->state_a == 0 && d->state_b == kStart + 120);
    CHECK(talk_after(d, 120) == kNone);  // ainda não (estritamente depois)
    const uint32_t waits[6] = {960, 150, 480, 240, 240, 300};
    for (uint32_t step = 0; step < 6; ++step) {
        const uint32_t line = talk_after(d, step == 0 ? 1 : waits[step - 1] + 1);
        CHECK(line == 0x52 + step);
        CHECK(d->state_a == step + 1 && d->state_b == GameState.frame + waits[step]);
    }
    CHECK(talk_after(d, 301) == kNone && d->state_a == 7);
    DCVars_tag dcv = dcv_of(d);
    GameState.frame += 1000;
    CHECK(NDrone2_BondTalk(&dcv) == 0 && line_now() == kNone);
    sound_debug_power_on();
}
