// Testes do HUD (hud.cpp) e da missão (mission.cpp): panes sintéticas (as tabelas de verdade vêm do
// ELF), um jogador num mundo vazio com a pistola e missões sintéticas na MissionData. Os sprites são
// os da camada 2D (Sprite_Create2 na SpriteList); sem arquivo de texto, só o rótulo 0 (uma string do
// StringHeap) tem texto.


#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/anim.h"
#include "platform/keys.h"
#include "engine/build.h"
#include "engine/collide.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/hud.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_input.h"
#include "test_framework.h"

namespace {

constexpr uint32_t kGunSkin = 0x5000100;
constexpr uint32_t kIdle = 0x6000b01;
constexpr uint32_t kFire = 0x6000b02;
// Uma fase sem regras próprias no Player_Init e no Player_InitWeapon (pele e armas padrão).
constexpr uint32_t kLevel = 0x7000030;

std::vector<uint32_t> make_seq(int16_t frames, uint8_t skeleton, int channels) {
    std::vector<uint32_t> w(0x10, 0);
    for (int c = 0; c < channels; ++c) w.push_back(4u | (uint32_t(frames) << 6) << 16);
    auto* hdr = reinterpret_cast<uint8_t*>(w.data());
    std::memcpy(hdr + 0x38, &frames, 2);
    const int16_t n = int16_t(channels);
    std::memcpy(hdr + 0x3a, &n, 2);
    hdr[0x3c] = skeleton;
    return w;
}

// Esqueleto de um osso, peles sem modelos e scripts vazios (como em test_weapon.cpp).
struct Assets {
    alignas(16) uint8_t skel[0x20] = {};
    uint8_t parents[1] = {0xff};
    ANIMSKIN_tag skin{};
    alignas(4) int16_t body[4] = {};
    alignas(4) uint16_t gun[8] = {0, 0, 30, 1, 0, 1, 30, 0x0b00};
    std::vector<uint32_t> seq = make_seq(30, 0, 3);
    Assets() {
        auto* k = reinterpret_cast<ANIMSKEL_tag*>(skel);
        k->id = 0;
        k->bone_count = 1;
        pSkeletons[0] = k;
        skin.parents = parents;
        skin.skeleton = 0;
        skin.scale[0] = skin.scale[1] = skin.scale[2] = 1.0f;
        for (uint32_t h : {0x5000007u, 0x50000abu, kGunSkin}) hashtable_additem(h, &skin);
        body[2] = 30;
        for (uint32_t h : {0x60001dcu, 0x60001ddu, 0x60009ffu, 0x6000a00u}) hashtable_additem(h, body);
        hashtable_additem(0x4000b00, seq.data());
        AnimProcessSeqData(0x4000b00);
        for (uint32_t h : {kIdle, kFire}) hashtable_additem(h, gun);
    }
};

// Pistola (arma 2): pente de 7, munição do tipo 3 (até 50), um tiro por aperto.
void define_weapons() {
    std::memset(weapon_data, 0, sizeof(weapon_data));
    std::memset(ammo_data, 0, sizeof(ammo_data));
    weapon_definition_tag* d = weapon_def(2);
    d->id = 2;
    d->klass = 2;
    d->selectable = 1;
    d->skin = kGunSkin;
    d->clip = 7;
    d->ammo_type = 3;
    d->rounds_per_shot = 1;
    d->bullets = 1;
    d->fire_delay = 10;
    d->burst[0] = 1;
    d->modes = 1;
    d->anim_idle = kIdle;
    d->anim_fire = kFire;
    d->zoom_max = 4.0f;
    weapon_def(1)->id = 1;
    weapon_def(1)->klass = 1;
    weapon_def(1)->selectable = 1;
    const int16_t max = 50;
    std::memcpy(ammo_data + 3 * 0xc + 2, &max, 2);
}

SpriteInfo image(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t mode, uint16_t flags = 0) {
    SpriteInfo s{};
    s.colour = 0x7f7f7fffu;
    s.colour2 = 0x7f7f7fffu;
    s.priority = mode;
    s.flags = flags;
    s.x = x;
    s.y = y;
    s.w = w;
    s.h = h;
    s.label = 0xffffffffu;
    s.viewer = 5;
    return s;
}

SpriteInfo text(int16_t x, int16_t y, uint16_t mode, uint16_t flags = 0) {
    SpriteInfo s{};
    s.colour = 0x7d6d59ffu;
    s.priority = mode;
    s.flags = flags;
    s.x = x;
    s.y = y;
    s.label = 0;  // string do heap
    s.viewer = 5;
    return s;
}

// Panes sintéticas: munição (0), vida (1), cortina de sangue (12) e luneta (6).
struct TestPanes {
    SpriteInfo ammo[8];
    SpriteInfo health[24];
    SpriteInfo blood[2];
    SpriteInfo sight[3];
    HUDPANECREATE_tag ammo_pane{};
    HUDPANECREATE_tag health_pane{};
    HUDPANECREATE_tag blood_pane{};
    HUDPANECREATE_tag sight_pane{};
    TestPanes() {
        ammo[0] = text(0, 18, 0x1c, 0x1020);
        ammo[1] = text(0, 38, 0x1c, 0x20);
        ammo[2] = text(0, 38, 0x1c, 0x1020);
        ammo[3] = text(-60, 0, 0xffff, 0x1020);
        ammo[4] = image(-62, -20, 68, 22, 0xffff, 0x1000);
        ammo[5] = image(-25, -64, 25, 64, 0x1c, 0x1000);
        ammo[6] = image(-25, -64, 25, 64, 0x1d, 0x1000);
        ammo[7] = image(0, 0, 0, 0, 0x1f, 0x900);
        ammo[3].flags = 0x20;  // a única de texto na caixa da pane, com a 1
        ammo_pane = {512, 448, 0, 0, HUD_CreateAmmoPane, HUD_UpdateAmmoPane, ammo, 8, 4, 0x18, 0};
        for (int i = 0; i < 24; ++i) health[i] = image(int16_t(2 * i), 10, 30, 30, uint16_t(0x10 + i % 8));
        for (int i = 0x14; i < 0x18; ++i) health[i].priority = 0xffff;
        health_pane = {16, 337, 0, 0, HUD_CreateHealthPane, HUD_UpdateHealthPane, health, 24, 0, 0, 0};
        blood[0] = image(0, 0, 512, 32, 9, 0x200);
        blood[1] = image(0, 0, 512, 64, 9, 0x200);
        blood_pane = {0, 0, 512, 448, HUD_CreateShrink, HUD_UpdateBloodPane, blood, 2, 0, 0x180, 0};
        for (int i = 0; i < 3; ++i) sight[i] = image(0, 0, 512, 448, 0x20);
        sight_pane = {0, 0, 512, 448, HUD_CreateShrink, HUD_UpdateSightPane, sight, 3, 0, 0x180, 0};
    }
};

// A missão da fase do teste: duas fases, a primeira com um objetivo revelado na chegada (canal 0x47)
// e outro revelado por ele (canal 0x48), a segunda com um objetivo (0x49).
struct TestMission {
    OBJECTIVE_tag first[2]{};
    OBJECTIVE_tag second[1]{};
    TestMission() {
        mission_debug_power_on();
        first[0].label = 0x4000006;
        first[0].fail_label = kNoLabel;
        first[0].channel = 0x47;
        first[1].label = 0x4000007;
        first[1].fail_label = kNoLabel;
        first[1].channel = 0x48;
        first[1].trigger = 0x47;
        first[1].state = 4;  // de uma partida anterior
        second[0].label = 0x4000008;
        second[0].fail_label = kNoLabel;
        second[0].channel = 0x49;
        second[0].channel_init = 0;
        second[0].state = 4;
        MissionData[0] = {kLevel, kLevel, 0, first, 2, {}};
        MissionData[1] = {kLevel + 1, kLevel, 1, second, 1, {}};
        // As missões do menu: o pacote em +0xc de cada entrada de 0x18 bytes.
        const uint32_t menu[4] = {0x9000001, 0x7000005, 0x9000005, kLevel};
        for (int i = 0; i < 4; ++i) std::memcpy(sp_level + i * 0x18 + 0xc, &menu[i], 4);
    }
};

void fresh_heap() {
    psiMemHeapSize = 2u << 20;
    PtrHeap = nullptr;
    Mem_Init();
}

struct HudWorld {
    TestPanes panes;
    TestMission mission;
    obj_tag* player = nullptr;
    BLData* b = nullptr;
    level_tag level{};
    HudWorld() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        // A tabela de hashes antes de Text_Init (as fontes procuram as texturas nela); vazia, a busca binária leria
        // fora da tabela.
        hashtable_debug_power_on();
        hashtable_initialise();
        object_debug_power_on();
        collide_debug_power_on();
        Coll_ResetHitHeap();
        view_debug_power_on();
        player_debug_power_on();
        player_weapon_debug_power_on();
        hud_debug_power_on();
        // A camada 2D como no ResetMap_GameInit, com uma tabela de rótulos mínima: o rótulo 0 (o texto
        // que o HUD escreve) é um buffer do StringHeap.
        txt_debug_power_on();
        static uint32_t fixups[1] = {0};
        FixupTable = fixups;
        NumFixups = 1;
        NumEntries = 1;
        sprite_debug_power_on();
        Sprite_InitLists();
        text_debug_power_on();
        Text_Init(0);
        control_init_object_lists();
        gbl_room_cels = nullptr;
        glb_world = build_alloc_world();
        std::memset(glb_players, 0, sizeof(glb_players));
        std::memset(switch_channels, 0, sizeof(switch_channels));
        std::memset(CheatInfo, 0, sizeof(CheatInfo));
        hashtable_debug_power_on();
        hashtable_initialise();
        anim_debug_power_on();
        static Assets* assets = nullptr;
        delete assets;
        assets = new Assets;
        define_weapons();
        std::memset(&GameState, 0, sizeof(GameState));
        GameState.difficulty = 2;
        GameState.level_file = kLevel;
        std::memset(MPSettings, 0, sizeof(MPSettings));
        const uint32_t one_player = 1;
        std::memcpy(MPSettings + 0x1ac, &one_player, 4);
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        Input_Init();
        GS_SetRefreshRate(60, 60);
        Plr_DMod_Normal = 1.0f;
        PaneList[kHudAmmo] = &panes.ammo_pane;
        PaneList[kHudHealth] = &panes.health_pane;
        PaneList[kHudBlood] = &panes.blood_pane;
        PaneList[kHudSight] = &panes.sight_pane;
        CrossHair = image(0, 0, 16, 16, 0x31);
        const int16_t frame1[9] = {1, 2, 29, 29, 0, 0, 0, 0, 0};
        std::memcpy(HUDCrossCoords[1], frame1, sizeof(frame1));
        BulletImg_189[3][0] = 10.0f;
        BulletImg_189[3][1] = 20.0f;
        BulletImg_189[3][2] = 8.0f;
        BulletImg_189[3][3] = 4.0f;
        // ResetMap_GameInit chama Mission_Init antes do Player_Start.
        Mission_Init(kLevel, 0);
        Camera_Create(0, glb_world, 0, 0, 0, 0x200, 0x1c0);
        _VECTOR pos{1.0f, 10.0f, 2.0f, 1.0f};
        _VECTOR rot{0.0f, 0.0f, 0.0f, 0.0f};
        player = Player_Init(0, &pos, &rot, &level);
        b = player != nullptr ? bloke_of(player) : nullptr;
        if (player != nullptr && anim_of(player) != nullptr) {
            anim_of(player)->root_offset = 1.1f;
            anim_of(player)->root_height = 1.1f;
        }
    }
    ~HudWorld() {
        // As panes do teste moram neste objeto: nada de ponteiros soltos para os outros testes.
        hud_debug_power_on();
        mission_debug_power_on();
        // As listas da camada 2D moram no heap deste teste.
        text_debug_power_on();
        sprite_debug_power_on();
        txt_debug_power_on();
    }
    HUDINFO_tag* hud() const { return static_cast<HUDINFO_tag*>(b->hud); }
    HUDPANE_tag& pane(uint32_t i) const { return hud()->panes[i]; }
    sprite* spr(uint32_t pane_index, uint32_t i) const { return pane(pane_index).sprites[i]; }
    void press(int vk, bool down) {
        g_psi_keyboard_mouse.active = true;
        g_psi_keyboard_mouse.down[vk] = down;
    }
    void tick(int n = 1) {
        for (int i = 0; i < n; ++i) {
            ++GameState.frame;
            Input_Update();
            Player_Update(player);
            Player_CollisionHandler(player);
            Camera_Update(0);
        }
    }
    // Segmentos acesos de uma barra: alfa acima do meio entre aceso e apagado.
    int lit(uint32_t first, uint32_t count, float threshold) const {
        int n = 0;
        for (uint32_t i = first; i < first + count; ++i) {
            const sprite* s = spr(kHudHealth, i);
            if (s->priority != -1 && float(s->colour & 0xff) > threshold * 255.0f) ++n;
        }
        return n;
    }
    // O texto que a pane escreveu (Sprite_SetText).
    std::string ammo_text() const {
        const sprite* s = spr(kHudAmmo, 0);
        return s->text != nullptr && s->text[0] != 0 ? std::string(s->text) : std::string("-");
    }
};

}  // namespace

TEST(hud_layout_box_and_safe_area) {
    // A caixa inclui a origem e ignora os sprites com a flag 0x1000.
    SpriteInfo s[3] = {image(10, 20, 30, 40, 0), image(-5, 4, 2, 2, 0), image(-500, -500, 1000, 1000, 0, 0x1000)};
    HUDPANECREATE_tag c{};
    c.sprites = s;
    c.count = 3;
    int16_t w = 0, h = 0;
    HUD_CalcWidthHeight(&c, &w, &h);
    CHECK(w == 45 && h == 60);  // x de -5 a 40, y de 0 a 60
    HUD_CalcWidthHeight(nullptr, &w, &h);  // nulo: nada muda
    CHECK(w == 45);
    // A área segura fica 16 pixels para dentro da tela da câmera; o que passa da borda volta.
    fresh_heap();
    view_debug_power_on();
    std::memset(MPSettings, 0, sizeof(MPSettings));
    glb_world = build_alloc_world();
    Camera_Create(0, glb_world, 0, 0, 0, 0x200, 0x1c0);
    int16_t x = 500, y = 440;
    HUD_ValidateXY(&x, &y, 60, 38, 0, 0x18);
    CHECK(x == 496 - 60 && y == 432 - 38);
    x = 4;
    y = 2;
    HUD_ValidateXY(&x, &y, 10, 10, 0, 0x18);
    CHECK(x == 4 + 16 && y == 2 + 16);  // abaixo da margem: soma a margem
    x = 3;
    y = 7;
    HUD_ValidateXY(&x, &y, 10, 10, 0, 0);  // sem flags: não mexe
    CHECK(x == 3 && y == 7);
}

TEST(hud_point_to_screen) {
    // Um ponto à frente da câmera cai no centro da tela; atrás, a resposta é -1.
    fresh_heap();
    view_debug_power_on();
    glb_world = build_alloc_world();
    Camera_Create(0, glb_world, 0, 0, 0, 0x200, 0x1c0);
    viewer_tag* v = glb_viewer[0];
    v->pos = {0.0f, 0.0f, 0.0f, 1.0f};
    v->view = MATRIX{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    v->proj_x = 300.0f;
    v->proj_y = 300.0f;
    _VECTOR p{0.0f, 0.0f, 10.0f, 1.0f}, out{};
    CHECK(View_3DPoint2Screen(&p, &out, 0) == 1);
    CHECK_NEAR(out.x, 256.0f, 1e-4f);
    CHECK_NEAR(out.y, 224.0f, 1e-4f);
    CHECK_NEAR(out.z, 10.0f, 1e-5f);
    // Mais longe, o mesmo deslocamento lateral aparece mais perto do centro.
    _VECTOR near_p{1.0f, 0.0f, 10.0f, 1.0f}, far_p{1.0f, 0.0f, 20.0f, 1.0f}, a{}, c{};
    View_3DPoint2Screen(&near_p, &a, 0);
    View_3DPoint2Screen(&far_p, &c, 0);
    CHECK(std::fabs(c.x - 256.0f) < std::fabs(a.x - 256.0f));
    _VECTOR behind{0.0f, 0.0f, -5.0f, 1.0f};
    CHECK(View_3DPoint2Screen(&behind, &out, 0) == -1 && out.z == 5.0f);
    CHECK(View_3DPoint2Screen(&p, &out, 3) == 0);  // câmera inexistente
    // Entrada e saída no mesmo vetor.
    _VECTOR same{0.0f, 0.0f, 10.0f, 7.0f};
    View_3DPoint2Screen(&same, &same, 0);
    CHECK_NEAR(same.x, 256.0f, 1e-4f);
    CHECK(same.w == 7.0f);
}

TEST(hud_init_panes_reset_and_state) {
    HudWorld w;
    CHECK(w.b != nullptr && w.b->hud != nullptr);
    HUDINFO_tag* hud = w.hud();
    CHECK(hud->crosshair != nullptr);
    // As panes da lista existem; as de situações especiais (luneta, cortina) começam desligadas.
    CHECK(w.pane(kHudAmmo).valid == 1 && w.pane(kHudAmmo).enabled == 1);
    CHECK(w.pane(kHudHealth).valid == 1 && w.pane(kHudHealth).enabled == 1);
    CHECK(w.pane(kHudBlood).valid == 1 && w.pane(kHudBlood).enabled == 0);
    CHECK(w.pane(kHudSight).valid == 1 && w.pane(kHudSight).enabled == 0);
    CHECK(w.pane(kHudAir).valid == 0 && HUD_PaneCount(w.b) == 4);
    CHECK(w.pane(kHudHealth).count == 24 && w.pane(kHudHealth).create == &w.panes.health_pane);
    // A pane de munição fica dentro da área segura (16 pixels das bordas da tela de 512x448).
    const HUDPANE_tag& ammo = w.pane(kHudAmmo);
    CHECK(16 <= ammo.x && ammo.x + int(w.panes.ammo_pane.w) <= 496);
    CHECK(16 <= ammo.y && ammo.y + int(w.panes.ammo_pane.h) <= 432);
    CHECK(w.spr(kHudAmmo, 1)->x == ammo.x && w.spr(kHudAmmo, 1)->y == ammo.y + 38);
    // HUD_Enable só mexe em panes que existem; HUD_State lê o estado.
    HUD_Enable(hud, kHudBlood, 1, 5);
    CHECK(w.pane(kHudBlood).enabled == 1 && HUD_State(hud, kHudBlood) == 5);
    HUD_Enable(hud, kHudAir, 1, 7);
    CHECK(w.pane(kHudAir).enabled == 0 && HUD_State(hud, kHudAir) == 0);
    CHECK(HUD_State(hud, kHudPanes) == 0 && HUD_State(nullptr, kHudBlood) == 0);
    HUD_Enable(nullptr, kHudBlood, 0, 0);  // nulo: nada
    // O byte +0x1a4 que Player_HandleDeath lê é a cortina ligada.
    CHECK(reinterpret_cast<const uint8_t*>(hud)[0x1a4] == 1);
    HUD_Reset(w.b);
    CHECK(w.pane(kHudBlood).enabled == 0 && w.pane(kHudAmmo).enabled == 1);
    HUD_DisableAll(w.b);
    for (const HUDPANE_tag& p : hud->panes) CHECK(p.enabled == 0);
    // Pane desligada: o tick esconde os sprites dela.
    w.tick();
    for (uint32_t i = 0; i < w.pane(kHudHealth).count; ++i) CHECK(w.spr(kHudHealth, i)->priority == -1);
    // Escondidos, nenhum sprite do HUD entra na lista de desenho da câmera 5.
    uint32_t first = 0, back = 0, count = 0;
    Sprite_BuildList(5, &first, &back, &count);
    CHECK(count == 0);
}

TEST(hud_health_and_armour_after_pain) {
    HudWorld w;
    w.tick();
    // Vida cheia: os 7 segmentos de vida acesos (0,55) e o colete (0) escondido.
    CHECK(w.lit(8, 7, 0.35f) == 7);
    for (uint32_t i = 0; i < 8; ++i) CHECK(w.spr(kHudHealth, i)->priority == -1);
    CHECK((w.spr(kHudHealth, 8)->colour & 0xffffff00u) == 0x52a88b00u);  // verde
    // 50 de dano: vida 50 = 3,5 segmentos (amarelo abaixo de 4) e um tiro conta no vermelho.
    Player_HandlePain(w.player, w.b, 50.0f, 0, -1);
    CHECK_NEAR(w.b->health, 50.0f, 1e-4f);
    w.tick();
    CHECK(w.lit(8, 7, 0.35f) == 4);
    CHECK((w.spr(kHudHealth, 8)->colour & 0xffffff00u) == 0xc6984e00u);
    // Os acesos são mais fortes que os apagados.
    CHECK((w.spr(kHudHealth, 8)->colour & 0xff) > (w.spr(kHudHealth, 14)->colour & 0xff));
    // Colete 50, tiro de 20: o colete absorve tudo (30 = 4,8 segmentos; o primeiro sempre cheio).
    w.b->armour = 50.0f;
    Player_HandlePain(w.player, w.b, 20.0f, 0, -1);
    CHECK_NEAR(w.b->armour, 30.0f, 1e-4f);
    CHECK_NEAR(w.b->health, 50.0f, 1e-4f);
    w.tick();
    CHECK(w.lit(0, 8, 0.55f) == 5);
    CHECK((w.spr(kHudHealth, 0)->colour & 0xff) == 0xff);
    CHECK((w.spr(kHudHealth, 1)->colour & 0xff) > (w.spr(kHudHealth, 7)->colour & 0xff));
    // Quase sem vida (menos de meio segmento): o primeiro pisca, vermelho.
    Player_HandlePain(w.player, w.b, 50.0f - 5.0f + 30.0f, 0, -1);
    CHECK_NEAR(w.b->health, 5.0f, 1e-3f);
    std::vector<uint32_t> alphas;
    for (int i = 0; i < 30; ++i) {
        w.tick();
        alphas.push_back(w.spr(kHudHealth, 8)->colour & 0xff);
    }
    CHECK((w.spr(kHudHealth, 8)->colour & 0xffffff00u) == 0x9d191200u);
    bool high = false, low = false;
    for (uint32_t a : alphas) {
        high = high || a > 100;
        low = low || a < 50;
    }
    CHECK(high && low);
    // A linha do relatório mostra o que as panes mostram.
    char line[256];
    HUD_Describe(w.b, line, sizeof(line));
    CHECK(std::strstr(line, "vida 5 (") != nullptr && std::strstr(line, "colete 0 (0/8)") != nullptr);
}

TEST(hud_pain_flash_fades_on_hit_arrows) {
    HudWorld w;
    w.tick();
    Player_HandlePain(w.player, w.b, 10.0f, 0, -1);
    CHECK(w.b->pain_flash == 128 && w.b->hit_dir == 0xc);
    w.tick();
    // hit_dir 0xc: os indicadores 2 e 3 (sprites 22 e 23) com o vermelho; o vermelho cai 1 por tick.
    CHECK(w.spr(kHudHealth, 0x16)->priority == 0x1e && w.spr(kHudHealth, 0x17)->priority == 0x1e);
    CHECK(w.spr(kHudHealth, 0x14)->priority == -1 && w.spr(kHudHealth, 0x15)->priority == -1);
    CHECK(w.spr(kHudHealth, 0x16)->colour == (0xff000000u | 128u));
    CHECK(w.b->pain_flash == 127);
    w.tick(100);
    CHECK(w.b->pain_flash == 27);
    CHECK(w.spr(kHudHealth, 0x17)->colour == (0xff000000u | 28u));
    w.tick(40);
    CHECK(w.b->pain_flash == 0);
    CHECK(w.spr(kHudHealth, 0x16)->priority == -1);
    char line[256];
    HUD_Describe(w.b, line, sizeof(line));
    CHECK(std::strstr(line, "sangue 0 (0)") != nullptr);
}

TEST(hud_ammo_after_fire_and_reload) {
    HudWorld w;
    // O nome da arma nova aparece no lugar da contagem por um segundo (FRAME_RATE ticks): a contagem
    // não é escrita e o sprite do nome (2) fica no lugar do modo de disparo (1).
    w.tick(3);
    CHECK(anim_of(w.player)->weapon == 2);
    CHECK(w.b->unknown_95e != 0);
    CHECK(w.spr(kHudAmmo, 2)->priority == 0x1c && w.spr(kHudAmmo, 1)->priority == -1);
    w.b->ammo[3] = 20;
    w.tick(70);
    CHECK(w.b->unknown_95e == 0 && w.spr(kHudAmmo, 2)->priority == -1 && w.spr(kHudAmmo, 1)->priority == 0x1c);
    CHECK(w.ammo_text() == "7-20");
    // O texto da munição é um sprite da SpriteList, medido (Sprite_SetText) e listado para desenho na
    // câmera dele (5).
    {
        sprite* text = w.spr(kHudAmmo, 0);
        CHECK(text->length == 4);
        uint32_t first = 0, back = 0, count = 0;
        P32<sprite>* list = Sprite_BuildList(5, &first, &back, &count);
        bool listed = false;
        for (uint32_t i = 0; i < count; ++i) listed = listed || list[i] == text;
        CHECK(listed);
    }
    // A pilha de balas: 7 de 4 pixels sobre o fundo do pente inteiro.
    CHECK(w.spr(kHudAmmo, 5)->h == 28 && w.spr(kHudAmmo, 6)->h == 28);
    CHECK(w.spr(kHudAmmo, 5)->u == 10 && w.spr(kHudAmmo, 5)->w == 8);
    w.press(nf::key::LButton, true);
    w.tick();
    w.press(nf::key::LButton, false);
    w.tick();
    CHECK(w.b->weapons[2].rounds == 6);
    CHECK(w.ammo_text() == "6-20");
    CHECK(w.spr(kHudAmmo, 5)->h == 24 && w.spr(kHudAmmo, 6)->h == 28);
    CHECK(w.spr(kHudAmmo, 5)->y == w.pane(kHudAmmo).y - 24);
    // Pente vazio: recarrega sozinho com a munição guardada.
    w.b->weapons[2].rounds = 0;
    w.tick(40);
    CHECK(w.b->weapons[2].rounds == 7 && w.b->ammo[3] == 13);
    CHECK(w.ammo_text() == "7-13");
    char line[256];
    HUD_Describe(w.b, line, sizeof(line));
    CHECK(std::strstr(line, "municao 7-13") != nullptr);
}

TEST(hud_crosshair_aim_and_scope) {
    HudWorld w;
    w.tick(3);
    sprite* cross = w.hud()->crosshair;
    // Sem mirar, com a opção de mira: no centro (mira 0,0), quadro 0, vermelha.
    CHECK(cross->priority == 0x31 && cross->colour == 0xff0000ffu);
    CHECK(cross->x == 256 && cross->y == 224);
    // Sem a opção, só aparece mirando.
    PlayerSetting[0].unknown_08 = 0;
    w.tick();
    CHECK(cross->priority == -1);
    // Mirando (sem luneta): o quadro 1 de HUDCrossCoords, na posição da mira.
    anim_of(w.player)->flags |= 1;
    w.b->aim_cur_x = 0.5f;
    w.b->aim_cur_y = 0.25f;
    HUD_UpdateCrossHair(w.b, reinterpret_cast<sprite*>(cross));
    CHECK(cross->priority == 0x31 && cross->u == 1 && cross->v == 2 && cross->w == 29 && cross->h == 29);
    CHECK(cross->x == 256 + 128 && cross->y == 224 - 56);
    // Arma de luneta (weapon_data +0x68 bit 0x40): a mira some e a pane da luneta liga.
    weapon_def(2)->flags |= 0x40;
    HUD_UpdateCrossHair(w.b, reinterpret_cast<sprite*>(cross));
    CHECK(cross->priority == -1 && w.pane(kHudSight).enabled == 1);
    // Parar de mirar desliga a luneta.
    anim_of(w.player)->flags &= ~1;
    PlayerSetting[0].unknown_08 = 1;
    HUD_UpdateCrossHair(w.b, reinterpret_cast<sprite*>(cross));
    CHECK(cross->priority == 0x31 && w.pane(kHudSight).enabled == 0);
    // Na visão noturna fica verde; com a cortina de sangue, escondida.
    glb_viewer[0]->mode = 1;
    HUD_UpdateCrossHair(w.b, reinterpret_cast<sprite*>(cross));
    CHECK(cross->colour == 0x208020ffu);
    glb_viewer[0]->mode = 0;
    HUD_Enable(w.hud(), kHudBlood, 1, 0);
    HUD_UpdateCrossHair(w.b, reinterpret_cast<sprite*>(cross));
    CHECK(cross->priority == -1);
}

TEST(hud_pickup_message_from_text_queue) {
    // A pane de itens (21) mostra a mensagem que Text_AddMsg pôs na fila: faixa (sprite 2) aberta, o
    // texto no sprite 0, na câmera 5; quando o tempo acaba, a faixa fecha e o texto some.
    HudWorld w;
    static SpriteInfo pickup[3];
    pickup[0] = text(0, 0, 0x1e, 0);
    pickup[1] = image(0, 0, 0, 0, 0x1d);
    pickup[2] = image(-100, -8, 200, 16, 0x1d);
    static HUDPANECREATE_tag pickup_pane{};
    pickup_pane = {256, 380, 0, 0, HUD_CreatePickupStatusPane, HUD_UpdateStatusPane, pickup, 3, 4, 0, 0};
    PaneList[kHudPickupStatus] = &pickup_pane;
    HUD_Init(w.b, w.player);
    CHECK(w.pane(kHudPickupStatus).valid != 0 && w.pane(kHudPickupStatus).extra[2] == 6);
    char line[] = "Picked up 20x 9mm rounds";
    Text_AddMsg(0, 0, 6, line, 0, 120);
    w.tick(4);
    sprite* s = w.spr(kHudPickupStatus, 0);
    CHECK(s->text != nullptr && std::strcmp(s->text, line) == 0 && s->priority == 0x1e);
    CHECK((s->colour & 0xff) == 0xff && w.spr(kHudPickupStatus, 2)->w == 200);
    uint32_t first = 0, back = 0, count = 0;
    P32<sprite>* list = Sprite_BuildList(5, &first, &back, &count);
    bool listed = false;
    for (uint32_t i = 0; i < count; ++i) listed = listed || list[i] == s;
    CHECK(listed);
    // 120 ticks e a faixa fecha 16 pixels por tick.
    w.tick(140);
    CHECK(w.pane(kHudPickupStatus).extra[1] == 0 && (s->colour & 0xff) == 0);
    PaneList[kHudPickupStatus] = nullptr;
}

TEST(hud_blood_curtain_then_mission_restart) {
    HudWorld w;
    w.tick();
    CHECK(Mission_Status() == kMissionRunning && InternalState == 1);
    // Morre: a cortina de sangue liga e desce um pixel por tick até cobrir a tela.
    Player_HandlePain(w.player, w.b, 500.0f, 0, -1);
    w.tick();
    CHECK(w.player->type == 0x12);
    w.tick();
    CHECK(w.pane(kHudBlood).enabled == 1 && HUD_State(w.hud(), kHudBlood) == 0);
    for (uint32_t i = 0; i < kHudPanes; ++i) {
        if (i != kHudBlood) CHECK(w.pane(i).enabled == 0);  // HUD_DisableAll
    }
    const int16_t y0 = w.spr(kHudBlood, 0)->y;
    w.tick(10);
    CHECK(w.spr(kHudBlood, 0)->y == y0 + 10 && w.spr(kHudBlood, 1)->h == 10);
    CHECK(switch_channels[0x62] == 0);
    w.tick(448);
    // Cobriu a tela: estado 1, que Player_HandleDeath espera para ligar o canal 0x62.
    CHECK(HUD_State(w.hud(), kHudBlood) == 1);
    CHECK(switch_channels[0x62] == 1);
    // A missão: canal 0x62 -> fracassada por morte, 1 s, e o menu de fim de missão com o jogo pausado
    // (sem arquivo de menu carregado o menu não abre; test_menu.cpp escolhe as opções dele).
    GameFlow_PushState(GF_WAIT_START, 0.0f, 0);
    Mission_Update();
    CHECK(InternalState == 3 && w.b->health == 0.0f);
    Mission_Update();
    CHECK(InternalState == 7 && Mission_Status() == kMissionDied && GameState.unknown_24 == 1);
    CHECK(PlrStat_OkToUpdate() == false);
    Mission_Update();
    CHECK(InternalState == 4);
    CHECK(g_mission_port.restarts == 0);
    GameState.load_file = 0;
    Mission_Update();
    CHECK(InternalState == 5 && g_mission_port.restarts == 0 && GameState.paused == 1);
    CHECK(GameState.load_file == 0 && GameFlow_GetState() == GF_WAIT_START);
    GS_PauseGame(0);
    while (GameFlow_Depth() > 0) GameFlow_PopState();
}

TEST(mission_objectives_reveal_and_complete) {
    HudWorld w;
    OBJECTIVE_tag* first = w.mission.first;
    OBJECTIVE_tag* second = w.mission.second;
    // Mission_Init (no fixture): a missão desta fase, os objetivos desta fase e das seguintes
    // recomeçam (0 revelado na chegada, 1 esperando o canal), os das anteriores ficam.
    CHECK(BaseMap == kLevel && ThisOrderNum == 0 && Mission_Status() == kMissionRunning);
    CHECK(first[0].state == 0 && first[1].state == 1 && second[0].state == 0);
    CHECK(Mission_NumVisObjectives() == 0);
    Mission_MonitorObjectives();
    // O da chegada é anunciado (ativo, 3); o da fase seguinte espera a fase dele.
    CHECK(first[0].state == 3 && first[1].state == 1 && second[0].state == 0);
    CHECK(Mission_NumVisObjectives() == 1);
    // O canal 0x47 cumpre o primeiro e revela o segundo.
    switch_channels[0x47] = 1;
    Mission_MonitorObjectives();
    CHECK(first[0].state == 4 && first[1].state == 3);
    CHECK(Mission_NumVisObjectives() == 2);
    OBJ_STATE st{};
    Mission_ObjectiveState(&st, 0);
    CHECK(st.state == 4 && st.status_label == 0x16f && st.label == 0x4000006);
    Mission_ObjectiveState(&st, 1);
    CHECK(st.state == 3 && st.status_label == 0x2000000 && st.label == 0x4000007);
    OBJ_STATE untouched{9, 9, 9, 9};
    Mission_ObjectiveState(&untouched, 5);
    CHECK(untouched.state == 9);
    // Nem tudo satisfeito (a fase seguinte ainda falta): sem o canal 100.
    switch_channels[0x48] = 1;
    Mission_MonitorObjectives();
    CHECK(first[1].state == 4 && switch_channels[100] == 0);
    const MissionSummary s = Mission_Summary();
    CHECK(s.total == 3 && s.done == 2 && s.failed == 0 && s.visible == 2);
    char line[256];
    Mission_Describe(line, sizeof(line));
    CHECK(std::strstr(line, "objetivos 2/3") != nullptr && std::strstr(line, "missao 1") != nullptr);
    // Reinício quente desta fase: os objetivos dela recomeçam (o canal volta ao valor inicial).
    Mission_Init(kLevel, 1);
    CHECK(first[0].state == 0 && first[1].state == 1 && switch_channels[0x47] == 0);
    // Na segunda fase (quente, da primeira): os da primeira ficam, os da segunda recomeçam.
    first[0].state = 4;
    first[1].state = 4;
    second[0].state = 3;
    GameState.level_file = kLevel + 1;
    Mission_Init(kLevel + 1, 1);
    CHECK(BaseMap == kLevel && ThisOrderNum == 1);
    CHECK(first[0].state == 4 && first[1].state == 4 && second[0].state == 0);
    switch_channels[0x47] = switch_channels[0x48] = 1;
    Mission_MonitorObjectives();
    CHECK(second[0].state == 3 && switch_channels[100] == 0);
    switch_channels[0x49] = 1;
    Mission_MonitorObjectives();
    CHECK(second[0].state == 4 && switch_channels[100] == 1 && switch_channels_time[100] == GameState.frame);
}

TEST(mission_complete_flow_to_front_end) {
    HudWorld w;
    // Segunda fase da missão, com os objetivos da primeira já cumpridos.
    GameState.level_file = kLevel + 1;
    Mission_Init(kLevel + 1, 1);
    w.mission.first[0].state = 4;
    w.mission.first[1].state = 4;
    switch_channels[0x47] = switch_channels[0x48] = switch_channels[0x49] = 1;
    GameFlow_PushState(GF_WAIT_START, 0.0f, 0);
    Mission_Update();  // passo 1: os objetivos se cumprem, o canal 100 liga
    CHECK(switch_channels[100] == 1 && InternalState == 6);
    Mission_Update();
    CHECK(Mission_Status() == kMissionComplete && InternalState == 7 && LevelToEndTo == 0x7000048);
    // 4 s de mensagem (VIDEO_FRAME_RATE x 4 ticks a 60 Hz), depois a fase de LevelToEndTo.
    int ticks = 0;
    while (InternalState == 7 && ticks < 1000) {
        Mission_Update();
        ++ticks;
    }
    CHECK(ticks == int(VIDEO_FRAME_RATE) * 4);
    CHECK(InternalState == 4);
    Mission_Update();
    CHECK(InternalState == 5 && GameState.load_file == 0x7000048 && GameState.menu_page == 0x40000036);
    // O resultado do menu principal continua no mapa das missões (P_NFMAP_Handler; tests/test_frontend.cpp).
    while (GameFlow_Depth() > 0) GameFlow_PopState();
}

TEST(mission_failed_objective_and_fail_labels) {
    HudWorld w;
    // Um objetivo "não deixe acontecer" (flag 2): ativo com o canal desligado, fracassa quando liga.
    w.mission.first[0].flags = 2;
    w.mission.first[0].fail_label = 0x4000099;
    Mission_Init(kLevel, 0);
    Mission_MonitorObjectives();
    CHECK(w.mission.first[0].state == 2);
    switch_channels[0x47] = 1;
    Mission_MonitorObjectives();
    CHECK(w.mission.first[0].state == 5 && switch_channels[99] == 1);
    Mission_MonitorObjectives();
    CHECK(Mission_FailLabel() == 0x4000099);
    // Os canais de falha geral trocam o rótulo.
    switch_channels[0x61] = 1;
    Mission_MonitorObjectives();
    CHECK(Mission_FailLabel() == 0x4000032);
    // Mission_Update: canal 99 -> fracassada, 5 s de mensagem, o menu de fim de missão (pausa).
    GameFlow_PushState(GF_WAIT_START, 0.0f, 0);
    InternalState = 1;
    Mission_Update();
    CHECK(InternalState == 2);
    Mission_Update();
    CHECK(Mission_Status() == kMissionFailed && InternalState == 7);
    int ticks = 0;
    while (InternalState == 7 && ticks < 1000) {
        Mission_Update();
        ++ticks;
    }
    CHECK(ticks == int(VIDEO_FRAME_RATE) * 5);
    Mission_Update();
    CHECK(InternalState == 5 && GameState.paused == 1);
    GS_PauseGame(0);
    while (GameFlow_Depth() > 0) GameFlow_PopState();
    // Fase sem missão: estado 5 (a vida não muda, como no jogo fora das missões).
    Mission_Init(0x7000999, 0);
    CHECK(Mission_Status() == kMissionNone && InternalState == 5 && BaseMap == 0xffffffffu);
    CHECK(PlrStat_OkToUpdate() == false);
    // O menu (07000048) não mexe na missão.
    Mission_SetStatus(kMissionComplete);
    Mission_Init(0x7000048, 0);
    CHECK(Mission_Status() == kMissionComplete && LevelToEndTo == 0x7000048);
    Mission_SetMapHCode(0x7000005);
    CHECK(Mission_BaseMapHCode() == 0x7000005);
}
