// Testes das páginas de opções do codinome no menu principal (engine/menu_options.cpp) e da regra das opções do jogo
// com o nightfire.ini (platform/game_options.h), num arquivo de menu feito à mão como em test_frontend.cpp: cada
// opção muda o seu byte de PlayerSetting[0] / DrawInfo / o som, "Controller Setup" mostra os nomes dos estilos (não
// os "1" do arquivo), o perfil gravado leva as opções de ida e volta, e as regras do .ini (o que ele aplica, o que um
// perfil padrão pega, o que um codinome lido não muda, o que uma mudança grava).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/loadsave.h"
#include "engine/memory.h"
#include "engine/menu.h"
#include "engine/menu_frontend.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/player_stats.h"
#include "engine/resetmap.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "platform/game_options.h"
#include "platform/input_bindings.h"
#include "platform/psi.h"
#include "platform/psi_card.h"
#include "platform/psi_file.h"
#include "platform/psi_input.h"
#include "platform/psi_sound.h"
#include "platform/settings.h"
#include "test_framework.h"
#include "ui/modern_options.h"

namespace {

// ---- arquivo de menu (docs/formats.md, "Menus (item tipo 8)") ----
struct OptFile {
    std::vector<uint8_t> b;
    size_t page_size_at = 0;
    OptFile() { dw(0xfffffffa); }
    void dw(uint32_t v) {
        for (int k = 0; k < 4; ++k) b.push_back(uint8_t(v >> (8 * k)));
    }
    void w(uint16_t v) {
        b.push_back(uint8_t(v));
        b.push_back(uint8_t(v >> 8));
    }
    void by(uint8_t v) { b.push_back(v); }
    void end_page() {
        if (page_size_at == 0) return;
        const uint32_t size = uint32_t(b.size() - page_size_at - 4);
        std::memcpy(b.data() + page_size_at, &size, 4);
        page_size_at = 0;
    }
    void page(uint32_t id) {
        end_page();
        dw(0xfffffff0);
        dw(id);
        dw(0x80000002);
        dw(0);
        by(0);
        page_size_at = b.size();
        dw(0);
    }
    void control(uint32_t id, uint8_t type, int16_t x, int16_t y, int16_t ww, int16_t h, uint32_t tag = 0,
                 uint32_t a = 0, uint32_t bb = 0) {
        dw(0xfffffff9);
        dw(id);
        by(type);
        by(0);
        w(uint16_t(x));
        w(uint16_t(y));
        w(uint16_t(ww));
        w(uint16_t(h));
        w(20);
        dw(tag);
        w(2);
        dw(a);
        dw(bb);
    }
    void msg(uint8_t m, uint32_t a, uint32_t bb = 0) {
        dw(0xfffffff6);
        by(m);
        dw(a);
        dw(bb);
    }
    void finish() {
        end_page();
        dw(0xfffffff2);
        dw(0);
    }
};

uint32_t fourcc(const char* s) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
           uint32_t(uint8_t(s[3])) << 24;
}

// Os textos: cada rótulo tem o seu ("L82", "L100018b"...), e os que as checagens leem pelo nome.
constexpr uint32_t kBankSize = 0x1000;
constexpr uint32_t kTable1 = 0x600;  // os rótulos 0x01xxxxxx começam aqui
char g_texts[kBankSize][16];
P32<char> g_bank[kBankSize];
uint32_t g_fixups[2] = {0, kTable1};
uint32_t bank_index(uint32_t label) { return (label >> 24) == 0 ? label : kTable1 + (label & 0xffffff); }
const char* text_of(uint32_t label) { return g_texts[bank_index(label)]; }

struct OptWorld {
    OptFile file;
    nf::MemoryCard card;
    std::vector<uint8_t> icon = std::vector<uint8_t>(0x128f8, 0x5a);
    OptWorld() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        hashtable_debug_power_on();
        hashtable_initialise();
        txt_debug_power_on();
        for (uint32_t i = 0; i < kBankSize; ++i) {
            if (i < kTable1) {
                std::snprintf(g_texts[i], sizeof(g_texts[i]), "L%x", i);
            } else {
                std::snprintf(g_texts[i], sizeof(g_texts[i]), "L%x", 0x1000000 + (i - kTable1));
            }
            g_bank[i] = g_texts[i];
        }
        g_texts[0][0] = 0;
        Bank = g_bank;
        NumEntries = kBankSize;
        FixupTable = g_fixups;
        NumFixups = 2;
        sprite_debug_power_on();
        Sprite_InitLists();
        text_debug_power_on();
        Text_Init(0);
        menu_debug_power_on();
        menu_handlers_debug_power_on();  // também o menu principal, as opções, o perfil, o cartão e o som
        nf::set_card(&card);
        SetUpIconFile(icon.data(), 0);
        mission_debug_power_on();
        player_stats_debug_power_on();
        for (uint32_t i = 0; i < kScoringTableCount; ++i) ScoringTable[i].rows = ScoringRows[i].row;
        std::memset(&GameState, 0, sizeof(GameState));
        GameState.difficulty = 2;
        std::memset(MPSettings, 0, sizeof(MPSettings));
        const uint32_t one_player = 1;
        std::memcpy(MPSettings + 0x1ac, &one_player, 4);
        std::memset(CheatInfo, 0, sizeof(CheatInfo));
        std::memset(def_codename, 0, sizeof(def_codename));
        DrawInfo[0] = DrawInfo[1] = DrawInfo[2] = 0;
        psiRestoreScreenPos();
        const char* ids[6] = {"PLRS", "MSSN", "MPSG", "GSET", "CHET", "BNUS"};
        SaveIFFBlocks[0] = SAVEIFFBLOCK{1, fourcc(ids[0]), &LS_MakePlrSettings, &LS_LoadPlrSettings};
        SaveIFFBlocks[1] = SAVEIFFBLOCK{2, fourcc(ids[1]), &LS_MakeMission, &LS_LoadMission};
        SaveIFFBlocks[2] = SAVEIFFBLOCK{4, fourcc(ids[2]), &LS_MakeMPSettings, &LS_LoadMPSettings};
        SaveIFFBlocks[3] = SAVEIFFBLOCK{8, fourcc(ids[3]), &LS_MakeGlobalSettings, &LS_LoadGlobalSettings};
        SaveIFFBlocks[4] = SAVEIFFBLOCK{0x10, fourcc(ids[4]), &LS_MakeCheats, &LS_LoadCheats};
        SaveIFFBlocks[5] = SAVEIFFBLOCK{0x20, fourcc(ids[5]), &LS_MakeBonus, &LS_LoadBonus};
        NumSaveBlocks = 6;
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        g_psi_keyboard_mouse.active = true;
        Input_Init();
        Input_Update();
        MenuManager_Init();
        GameFlow_PushState(GF_WAIT_START, 0.0f, 0);
        // A página de onde as opções vêm (o menu do codinome tem o seu tratador; aqui só a janela).
        file.page(0x4000001d);
        file.control(0x4000001d, MCT_WINDOW, 0, 0, 640, 480);
    }
    ~OptWorld() {
        for (uint8_t i = 0; i < kMenuManagers; ++i) MenuManager_Delete(i);
        menu_debug_power_on();
        menu_handlers_debug_power_on();
        nf::set_card(nullptr);
        SetUpIconFile(nullptr, 0);
        for (uint32_t i = 0; i < kScoringTableCount; ++i) ScoringTable[i] = SCORETABLE{};
        player_stats_debug_power_on();
        mission_debug_power_on();
        text_debug_power_on();
        sprite_debug_power_on();
        txt_debug_power_on();
        std::memset(SaveIFFBlocks, 0, sizeof(SaveIFFBlocks));
        NumSaveBlocks = 0;
        std::memset(def_codename, 0, sizeof(def_codename));
        std::memset(sp_level, 0, sizeof(sp_level));
        std::memset(menu_bonus, 0, sizeof(menu_bonus));
        DrawInfo[0] = DrawInfo[1] = DrawInfo[2] = 0;
        psiRestoreScreenPos();
        Input_Init();
        while (GameFlow_Depth() > 0) GameFlow_PopState();
    }
    // Abre o menu na página do codinome e vai a `page` (0x4c com a anterior 0x4000001d, como no jogo).
    void open(uint32_t page) {
        // A última página criada recebe a saída (0x4d) quando MenuManager_Create mostra a primeira; no disco ela é a do
        // fim do jogo, aqui uma sem tratador.
        file.page(0x40000052);
        file.control(0x40000052, MCT_WINDOW, 0, 0, 640, 480);
        file.finish();
        MenuManager_Load(0x8000002, file.b.data());
        MenuManager_Create(0x80000002u, 0x4000001d, -1, 0, 0x28, 0, 5);
        tick(2);
        go(page);
    }
    void go(uint32_t page) {
        Manager_SendMessage(&menu_manager[0], 0x44, int32_t(page), 0);
        tick(3);
    }
    void tick(int n = 1) {
        for (int i = 0; i < n; ++i) {
            std::memset(g_psi_keyboard_mouse.down, 0, sizeof(g_psi_keyboard_mouse.down));
            Input_Update();
            MenuManager_Update();
            MenuManager_Monitor();
        }
    }
    // Um botão do controle do PS2 por um tick (0x1f X escolher, 0x21 círculo, 0x22 triângulo voltar).
    void press(uint32_t action) {
        std::memset(g_psi_keyboard_mouse.down, 0, sizeof(g_psi_keyboard_mouse.down));
        Input_Update();
        Input_SetAction(0, action, kActionDown | kActionPressed);
        MenuManager_Update();
        MenuManager_Monitor();
        tick(2);
    }
    void focus(uint32_t id) {
        Manager_SendMessage(&menu_manager[0], 0x22, int32_t(id), 0);
        tick();
    }
    M_CONTROL* control(uint32_t id, uint32_t tag = 0, uint8_t type = 0) {
        M_PAGE* p = menu_manager[0].page;
        for (M_CONTROL* c = p != nullptr ? static_cast<M_CONTROL*>(ll_first(p->controls)) : nullptr; c != nullptr;
             c = c->next) {
            if (c->id == id && c->tag == tag && (type == 0 || c->type == type)) return c;
        }
        return nullptr;
    }
    M_RADIO* radio(uint32_t id) { return reinterpret_cast<M_RADIO*>(control(id, 0, MCT_RADIO)); }
    M_SCROLL* bar(uint32_t id, uint32_t tag = 0) { return reinterpret_cast<M_SCROLL*>(control(id, tag, MCT_SCROLL)); }
    const char* label_text(uint32_t id, uint32_t tag = 0) {
        const M_LABEL* l = reinterpret_cast<const M_LABEL*>(control(id, tag, MCT_LABEL));
        return l != nullptr && l->string != nullptr ? nf::raw(l->string) : "";
    }
};

int32_t radio_value(M_RADIO* r) { return r != nullptr && r->selected != nullptr ? r->selected->data : -999; }
const char* radio_text(M_RADIO* r) { return r != nullptr && r->selected != nullptr ? r->selected->cells[0].text : ""; }
void set_radio(uint32_t id, int32_t value) { __Menu_Send(0, id, 0x1e, value, 0); }

// As rodas de uma página de opções: a roda e o rótulo do nome (desabilitado, como no disco).
void option_row(OptFile& f, uint32_t id, int16_t y) {
    f.control(id, MCT_RADIO, 348, y, 219, 16);
    f.control(id, MCT_LABEL, 54, y, 301, 16);
    f.msg(0x18, 0, 2);
    f.msg(0x2b, 6);
}

// LS_Save/LS_Load com o cartão em memória até o fim (7 ou 8); responde "sim" ao sobrescrever.
uint16_t save_profile(const char* name) {
    char buf[0x20];
    std::snprintf(buf, sizeof(buf), "%s", name);
    LS_FlushStates();
    for (int i = 0; i < 400 && Save_State != 7 && Save_State != 8; ++i) {
        LS_Save(buf, 0, 0xff);
        PS2PollMemoryCard();
        if (PS2CardFlow.message == 4 && PS2CardFlow.answered == 0) {
            PS2CardFlow.answer = 2;
            PS2CardFlow.answered = 1;
        }
    }
    return Save_State;
}
uint16_t load_profile(const char* name) {
    char buf[0x20];
    std::snprintf(buf, sizeof(buf), "%s", name);
    LS_FlushStates();
    for (int i = 0; i < 400 && Load_State != 7 && Load_State != 8; ++i) {
        LS_Load(buf, 0, 0xff);
        PS2PollMemoryCard();
    }
    return Load_State;
}

}  // namespace

TEST(options_controller_setup_shows_style_names_and_saves) {
    OptWorld w;
    OptFile& f = w.file;
    // "Controller Setup" do PS2 (0x40000022): a roda dos estilos, os rótulos ao redor do controle com o "1" do arquivo,
    // duas linhas (uma do estilo 2, outra do estilo 5) e o botão da inversão.
    f.page(0x40000022);
    f.control(0x4000000e, MCT_WINDOW, 0, 0, 640, 480);
    const uint32_t ids[15] = {0x10000062, 0x10000063, 0x10000064, 0x10000065, 0x10000066, 0x10000067, 0x10000068, 0x10000069,
                              0x1000006a, 0x1000006b, 0x1000006c, 0x1000006d, 0x1000006e, 0x100000c8, 0x10000163};
    for (uint32_t i = 0; i < 15; ++i) {
        f.control(ids[i], MCT_LABEL, 24, int16_t(165 + 10 * i), 185, 17);
        f.msg(0x18, 0, 2);
    }
    f.control(0x10000061, MCT_LABEL, 375, 174, 47, 2, 1u << 2);
    f.control(0x10000061, MCT_LABEL, 216, 299, 75, 2, 1u << 5);
    f.control(0x10000025, MCT_RADIO, 60, 109, 316, 24);
    f.control(0x100000c9, MCT_BUTTON, 461, 109, 99, 25);
    f.msg(0x18, 0, 0x206);
    PlayerSetting[0].style = 2;
    PlayerSetting[0].invert_look = 1;
    w.open(0x40000022);
    CHECK(Menu_CurrentPage(0) == 0x40000022);
    // Os oito estilos com os nomes do texto (0x82, 0x91... 0x100019a), no estilo do jogador.
    M_RADIO* styles = w.radio(0x10000025);
    CHECK(styles != nullptr && styles->options.count == 8);
    static const uint32_t kNames[8] = {0x82, 0x91, 0xa0, 0xaf, 0xbe, 0xcd, 0x100018b, 0x100019a};
    int i = 0;
    for (const LIST_ROW* r = styles != nullptr ? static_cast<const LIST_ROW*>(ll_first(styles->options)) : nullptr;
         r != nullptr; r = r->next, ++i) {
        CHECK(std::strcmp(r->cells[0].text, text_of(kNames[i])) == 0 && r->data == i);
    }
    CHECK(radio_value(styles) == 2);
    // O controle desenhado no estilo 2: rótulos consecutivos a partir de 0xa1 (0x100000c8), e não o "1" do arquivo.
    CHECK(std::strcmp(w.label_text(0x100000c8), text_of(0xa1)) == 0);
    CHECK(std::strcmp(w.label_text(0x10000062), text_of(0xa2)) == 0);
    CHECK(std::strcmp(w.label_text(0x1000006e), text_of(0xae)) == 0);
    CHECK(std::strcmp(w.label_text(0x10000163), text_of(0x2f7)) == 0);
    CHECK((w.control(0x10000061, 1u << 2)->state & 3) == 2 && (w.control(0x10000061, 1u << 5)->state & 1) == 1);
    const M_BUTTON* invert = reinterpret_cast<const M_BUTTON*>(w.control(0x100000c9));
    CHECK(invert != nullptr && std::strcmp(invert->label.string, text_of(0x206)) == 0);  // "Inverted"
    // Mudar a roda redesenha (o estilo 6, rótulos de 0x100018c) sem gravar.
    set_radio(0x10000025, 6);
    CHECK(std::strcmp(w.label_text(0x10000062), text_of(0x100018d)) == 0);
    CHECK(std::strcmp(w.label_text(0x10000163), text_of(0x10001cd)) == 0);
    CHECK(PlayerSetting[0].style == 2);
    // O círculo alterna a inversão mostrada; escolher grava o estilo e a inversão e volta.
    w.focus(0x10000025);
    w.press(0x21);
    CHECK(was_inverted == 0 && std::strcmp(invert->label.string, text_of(0x3bd)) == 0);  // "Normal"
    CHECK(PlayerSetting[0].invert_look == 1);
    cn_modified_flag = 0;
    w.press(0x1f);
    CHECK(PlayerSetting[0].style == 6 && PlayerSetting[0].invert_look == 0 && cn_modified_flag == 1);
    CHECK(Menu_CurrentPage(0) == 0x4000001d);
    CHECK(nf::game_option(nf::GameOption::PadStyle) == 6);
}

TEST(options_advanced_page_writes_each_player_setting_byte) {
    OptWorld w;
    OptFile& f = w.file;
    f.page(0x4000002d);
    f.control(0x4000002d, MCT_WINDOW, 0, 0, 640, 480);
    const uint32_t rows[8] = {0x1000011a, 0x1000011b, 0x10000124, 0x10000125, 0x10000126, 0x100001cb, 0x10000226, 0x1000023b};
    for (int i = 0; i < 8; ++i) option_row(f, rows[i], int16_t(104 + 32 * i));
    PlayerInput_tag& p = PlayerSetting[0];
    p.unknown_09 = 1;    // vibração
    p.autoaim = 0;
    p.unknown_08 = 1;    // mira sem mirar
    p.crouch_toggle = 0;  // segurar
    p.aim_toggle = 1;     // alternar
    p.auto_switch = 0;
    p.unknown_0c = 2;    // itens piscando
    p.unknown_0b = 1;    // HUD não fica sempre
    w.open(0x4000002d);
    // Cada roda mostra o byte dela, com os textos do jogo (On/Off, Toggle/Hold).
    CHECK(radio_value(w.radio(0x1000011a)) == 1 && std::strcmp(radio_text(w.radio(0x1000011a)), text_of(0x1b7)) == 0);
    CHECK(radio_value(w.radio(0x1000011b)) == 0 && std::strcmp(radio_text(w.radio(0x1000011b)), text_of(0x1b6)) == 0);
    CHECK(radio_value(w.radio(0x10000124)) == 1);
    CHECK(radio_value(w.radio(0x10000125)) == 0 && std::strcmp(radio_text(w.radio(0x10000125)), text_of(0x2a3)) == 0);
    CHECK(radio_value(w.radio(0x10000126)) == 1 && std::strcmp(radio_text(w.radio(0x10000126)), text_of(0x2a2)) == 0);
    CHECK(radio_value(w.radio(0x100001cb)) == 0);
    CHECK(radio_value(w.radio(0x10000226)) == 2 && std::strcmp(radio_text(w.radio(0x10000226)), text_of(0x1b7)) == 0);
    CHECK(radio_value(w.radio(0x1000023b)) == 1 && std::strcmp(radio_text(w.radio(0x1000023b)), text_of(0x1b6)) == 0);
    // Mudar as rodas não grava; voltar (triângulo) deixa tudo como estava.
    set_radio(0x1000011b, 1);
    w.press(0x22);
    CHECK(Menu_CurrentPage(0) == 0x4000001d && p.autoaim == 0);
    // De novo: cada roda trocada e escolher (X numa delas) grava todos os bytes.
    w.go(0x4000002d);
    set_radio(0x1000011a, 0);
    set_radio(0x1000011b, 1);
    set_radio(0x10000124, 0);
    set_radio(0x10000125, 1);
    set_radio(0x10000126, 0);
    set_radio(0x100001cb, 1);
    set_radio(0x10000226, 0);
    set_radio(0x1000023b, 0);
    w.focus(0x1000011b);
    cn_modified_flag = 0;
    w.press(0x1f);
    CHECK(p.unknown_09 == 0 && p.autoaim == 1 && p.unknown_08 == 0 && p.crouch_toggle == 1);
    CHECK(p.aim_toggle == 0 && p.auto_switch == 1 && p.unknown_0c == 0 && p.unknown_0b == 0);
    CHECK(cn_modified_flag == 1 && Menu_CurrentPage(0) == 0x4000001d);
    // As mesmas opções pelos acessores do port, nas unidades do menu.
    using nf::GameOption;
    CHECK(nf::game_option(GameOption::Vibration) == 0 && nf::game_option(GameOption::AutoAim) == 1);
    CHECK(nf::game_option(GameOption::Crosshairs) == 0 && nf::game_option(GameOption::CrouchToggle) == 1);
    CHECK(nf::game_option(GameOption::AimToggle) == 0 && nf::game_option(GameOption::AutoSwitch) == 1);
    CHECK(nf::game_option(GameOption::FlashingObjects) == 0 && nf::game_option(GameOption::HudAlwaysOn) == 1);
    nf::set_game_option(GameOption::FlashingObjects, 1);
    nf::set_game_option(GameOption::HudAlwaysOn, 0);
    CHECK(p.unknown_0c == 2 && p.unknown_0b == 1);
}

TEST(options_multiplayer_page_stays_in_the_profile_values) {
    OptWorld w;
    OptFile& f = w.file;
    f.page(0x4000002e);
    f.control(0x4000002e, MCT_WINDOW, 0, 0, 640, 480);
    option_row(f, 0x10000128, 174);
    option_row(f, 0x1000011c, 207);
    option_row(f, 0x10000129, 239);
    const int32_t handicap = -25;
    std::memcpy(MPSettings + 0x2c, &handicap, 4);
    PlayerSetting[0].autoaim_mp = 1;
    w.open(0x4000002e);
    M_RADIO* h = w.radio(0x1000011c);
    CHECK(h != nullptr && h->options.count == 8 && radio_value(h) == -25 && std::strcmp(radio_text(h), "-25") == 0);
    set_radio(0x1000011c, 100);
    set_radio(0x10000128, 1);
    set_radio(0x10000129, 0);
    w.focus(0x1000011c);
    w.press(0x1f);
    int32_t radar = 0, value = 0;
    std::memcpy(&radar, MPSettings + 0x28, 4);
    std::memcpy(&value, MPSettings + 0x2c, 4);
    CHECK(radar == 1 && value == 100 && PlayerSetting[0].autoaim_mp == 0);
}

TEST(options_av_page_volumes_subtitles_screen_and_restore) {
    OptWorld w;
    OptFile& f = w.file;
    f.page(0x40000031);
    f.control(0x40000031, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x10000135, MCT_LABEL, 55, 86, 301, 16);
    f.control(0x10000135, MCT_SCROLL, 349, 86, 216, 16, 0, 0, 100);
    f.control(0x10000134, MCT_LABEL, 56, 114, 301, 16);
    f.control(0x10000134, MCT_SCROLL, 350, 114, 219, 16, 0, 0, 100);
    option_row(f, 0x10000136, 142);
    option_row(f, 0x1000019a, 170);
    option_row(f, 0x10000198, 198);
    option_row(f, 0x10000223, 227);
    f.control(0x10000137, MCT_LABEL, 56, 256, 301, 16);
    f.msg(0x2b, 4);
    f.control(0x100001a3, MCT_BUTTON, 56, 284, 301, 16);
    f.control(0x10000138, MCT_LABEL, 56, 312, 301, 16);
    f.msg(0x2b, 4);
    // O ajuste de tela (0x40000047): as duas barras de C_SBSCREEN_Handler (tag 1 horizontal, 0 vertical).
    f.page(0x40000047);
    f.control(0x40000047, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x100001eb, MCT_SCROLL, 256, 224, 128, 32, 1, 0, 10);
    f.control(0x100001eb, MCT_SCROLL, 304, 176, 32, 128, 0, 0, 10);
    SFXMusicSetVolume(50);
    SFXSetVolume(80);
    SFXSetMode(4);
    DrawInfo[0] = 1;
    DrawInfo[1] = 1;
    DrawInfo[2] = 0;
    psiSetScreenPos(24, -8);
    w.open(0x40000031);
    CHECK(Menu_CurrentPage(0) == 0x40000031);
    // As barras de 0 a 20 (o volume / 5) e as rodas com os valores do jogo.
    M_SCROLL* music = w.bar(0x10000135);
    M_SCROLL* sfx = w.bar(0x10000134);
    CHECK(music != nullptr && music->max == 20 && music->pos == 10 && sfx != nullptr && sfx->pos == 16);
    CHECK(radio_value(w.radio(0x10000136)) == 0 && radio_value(w.radio(0x1000019a)) == 1);
    CHECK(radio_value(w.radio(0x10000198)) == 4 && w.radio(0x10000198)->options.count == 3);
    CHECK(radio_value(w.radio(0x10000223)) == 1 && w.radio(0x10000223)->options.count == 2);
    // O volume segue a barra na hora (para ouvir); voltar sem escolher põe o de antes.
    __Menu_Send(0, 0x10000135, 0x2e, 4, 0);
    w.tick();
    CHECK(SFXMusicGetVolume() == 20);
    w.press(0x22);
    CHECK(Menu_CurrentPage(0) == 0x4000001d && SFXMusicGetVolume() == 50 && SFXGetVolume() == 80);
    // Peculiaridade do jogo: a roda de widescreen não é limpa, cada visita acrescenta "On" e "Off".
    w.go(0x40000031);
    CHECK(w.radio(0x10000223)->options.count == 4);
    // Escolher grava tudo: legendas, tela dividida, o som, widescreen e os volumes.
    __Menu_Send(0, 0x10000135, 0x2e, 6, 0);
    set_radio(0x10000136, 1);
    set_radio(0x1000019a, 0);
    set_radio(0x10000198, 0);
    set_radio(0x10000223, 0);
    w.focus(0x10000136);
    cn_modified_flag = 0;
    w.press(0x1f);
    CHECK(Menu_CurrentPage(0) == 0x4000001d && cn_modified_flag == 1);
    CHECK(DrawInfo[2] == 1 && DrawInfo[0] == 0 && DrawInfo[1] == 0 && SFXGetMode() == 0);
    CHECK(SFXMusicGetVolume() == 30 && SFXGetVolume() == 80);
    CHECK(nf::game_option(nf::GameOption::Subtitles) == 1 && nf::game_option(nf::GameOption::MusicVolume) == 30);
    // "Restore Defaults": música 70, efeitos 100, legendas desligadas na versão americana, horizontal, estéreo, sem
    // widescreen e a tela no centro; nada gravado até escolher.
    LanguageToLoad = 6;
    w.go(0x40000031);
    w.focus(0x100001a3);
    w.press(0x1f);
    CHECK(Menu_CurrentPage(0) == 0x40000031);
    CHECK(w.bar(0x10000135)->pos == 14 && w.bar(0x10000134)->pos == 20);
    CHECK(radio_value(w.radio(0x10000136)) == 0 && radio_value(w.radio(0x1000019a)) == 0);
    CHECK(radio_value(w.radio(0x10000198)) == 1 && radio_value(w.radio(0x10000223)) == 0);
    int32_t x = 1, y = 1;
    psiGetScreenPos(&x, &y);
    CHECK(x == 0 && y == 0);
    CHECK(DrawInfo[2] == 1);
    // O ajuste de tela: "Screen Adjust" vai à página; cada passo de uma barra move a posição guardada (x de 12 em
    // 12 até ±0x1b0, y de 4 em 4 até ±0x30) e a barra volta ao meio; voltar põe a de antes, escolher fica com a nova.
    psiSetScreenPos(24, -8);
    w.focus(0x10000137);
    w.press(0x1f);
    CHECK(Menu_CurrentPage(0) == 0x40000047);
    M_SCROLL* hbar = w.bar(0x100001eb, 1);
    M_SCROLL* vbar = w.bar(0x100001eb, 0);
    CHECK(hbar != nullptr && hbar->min == -1 && hbar->max == 1 && vbar != nullptr);
    __Menu_SendMessage(&hbar->base, 0x2e, 1, 0);
    C_SBSCREEN_Handler(0, &hbar->base, hbar->base.id, 0x49, 0, 0);
    __Menu_SendMessage(&vbar->base, 0x2e, -1, 0);
    C_SBSCREEN_Handler(0, &vbar->base, vbar->base.id, 0x49, 0, 0);
    psiGetScreenPos(&x, &y);
    CHECK(x == 36 && y == -12 && hbar->pos == 0 && vbar->pos == 0);
    for (int i = 0; i < 60; ++i) psiAdjustScreenPos(1, 1);
    psiGetScreenPos(&x, &y);
    CHECK(x == 0x1b0 && y == 0x30);
    w.press(0x22);
    psiGetScreenPos(&x, &y);
    CHECK(Menu_CurrentPage(0) == 0x40000031 && x == 24 && y == -8);
    w.focus(0x10000137);
    w.press(0x1f);
    psiAdjustScreenPos(-1, 0);
    w.focus(0x100001eb);
    w.press(0x1f);
    psiGetScreenPos(&x, &y);
    CHECK(Menu_CurrentPage(0) == 0x40000031 && x == 12 && y == -8);
}

TEST(options_credits_scroll_from_the_imported_table) {
    OptWorld w;
    OptFile& f = w.file;
    f.page(0x40000030);
    f.control(0x40000030, MCT_WINDOW, 0, 0, 640, 480);
    for (uint32_t t = 0; t < 26; ++t) {
        f.control(0x10000213, MCT_LABEL, 58, int16_t(-170 + 30 * int(t)), 254, 16, t);
        f.control(0x10000214, MCT_LABEL, 328, int16_t(-138 + 30 * int(t)), 254, 16, t);
    }
    f.control(0x10000215, MCT_LABEL, 0, 0, 640, 480);
    f.control(0x1000023c, MCT_LABEL, 0, 0, 640, 480);
    // Três linhas: uma larga com rótulo, uma em duas colunas com strings, uma com rótulo e string.
    Menu_PortClearTables();
    CreditsSource[0] = CREDIT_SOURCE{0x10002a5, nullptr, 0, nullptr, 0, 0, 1};
    CreditsSource[1] = CREDIT_SOURCE{0, Menu_PortTableString("Producer"), 0, Menu_PortTableString("Jane"), 1, 2, 0};
    CreditsSource[2] = CREDIT_SOURCE{0x10002a6, nullptr, 0, Menu_PortTableString("Joe"), 0, 0, 0};
    CreditsSourceCount = 3;
    SFXMusicSetVolume(70);
    w.open(0x40000030);
    CHECK(Menu_CurrentPage(0) == 0x40000030 && ptr_credits != nullptr);
    // A cada 14 ticks uma linha entra em y = 0x200 e todas sobem 2 por tick (no mesmo tick também).
    M_CONTROL* first = w.control(0x10000213, 0);
    for (int i = 0; i < 30 && first != nullptr && (first->state & 1) != 0; ++i) w.tick();
    CHECK(first != nullptr && first->y == 0x200 - 2 && first->w == 0x1e0 && (first->state & 1) == 0);
    CHECK(std::strcmp(w.label_text(0x10000213, 0), text_of(0x10002a5)) == 0);
    CHECK((w.control(0x10000214, 0)->state & 1) == 1);  // o cargo escondido numa linha larga
    w.tick(14);
    M_CONTROL* second = w.control(0x10000213, 1);
    CHECK(second->x == 0x10 && second->w == 0x100 && w.control(0x10000214, 1)->x == 0xf0);
    CHECK(std::strcmp(w.label_text(0x10000213, 1), "Producer") == 0 && std::strcmp(w.label_text(0x10000214, 1), "Jane") == 0);
    CHECK(first->y == 0x200 - 2 - 28);
    // Com 3 linhas a música baixa logo (count - 50 vira um número enorme sem sinal: não baixa) e 18 linhas depois da
    // última a página volta à anterior.
    CHECK(SFXMusicGetVolume() == 70);
    w.tick(14 * 22);
    CHECK(Menu_CurrentPage(0) == 0x4000001d && SFXMusicGetVolume() == 70);
    Menu_PortClearTables();
}

TEST(options_profile_round_trip_carries_the_options) {
    OptWorld w;
    using nf::GameOption;
    // Cada opção com um valor diferente do padrão.
    nf::set_game_option(GameOption::Vibration, 0);
    nf::set_game_option(GameOption::AutoAim, 0);
    nf::set_game_option(GameOption::Crosshairs, 0);
    nf::set_game_option(GameOption::CrouchToggle, 0);
    nf::set_game_option(GameOption::AimToggle, 1);
    nf::set_game_option(GameOption::AutoSwitch, 0);
    nf::set_game_option(GameOption::FlashingObjects, 0);
    nf::set_game_option(GameOption::HudAlwaysOn, 0);
    nf::set_game_option(GameOption::PadStyle, 3);
    nf::set_game_option(GameOption::InvertLook, 1);
    nf::set_game_option(GameOption::Subtitles, 1);
    nf::set_game_option(GameOption::MusicVolume, 35);
    nf::set_game_option(GameOption::EffectsVolume, 55);
    nf::set_game_option(GameOption::SpeakerMode, 4);
    nf::set_game_option(GameOption::Widescreen, 1);
    nf::set_game_option(GameOption::SplitScreen, 1);
    psiSetScreenPos(-36, 8);
    int32_t before[nf::kGameOptionCount];
    for (int i = 0; i < nf::kGameOptionCount; ++i) before[i] = nf::game_option(GameOption(i));
    CHECK(save_profile("OPTS") == 7);
    // Tudo de volta aos padrões e o perfil lido: as opções voltam.
    Input_Init();
    psi_sound_debug_power_on();
    DrawInfo[0] = DrawInfo[1] = DrawInfo[2] = 0;
    psiRestoreScreenPos();
    CHECK(nf::game_option(GameOption::PadStyle) == 0 && nf::game_option(GameOption::MusicVolume) == 70);
    CHECK(load_profile("OPTS") == 7);
    for (int i = 0; i < nf::kGameOptionCount; ++i) CHECK(nf::game_option(GameOption(i)) == before[i]);
    int32_t x = 0, y = 0;
    psiGetScreenPos(&x, &y);
    CHECK(x == -36 && y == 8);
}

TEST(options_ini_rules_boot_default_profile_codename_and_change) {
    OptWorld w;
    using nf::GameOption;
    const nf::ControlsConfig saved_controls = nf::controls();
    // O .ini com algumas opções do jogo (as outras ficam com o padrão do jogo).
    nf::Settings s;
    s.parse("[controls]\npad_style = 4\n[ui]\nsubtitles = 1\n[game]\nauto_aim = 0\ncrouch = hold\nmusic_volume = 40\n");
    nf::load_controls(s, &nf::controls());
    nf::load_game_options(s, &nf::game_options_ini());
    const nf::GameOptionsIni& g = nf::game_options_ini();
    CHECK(g.known[int(GameOption::PadStyle)] && g.value[int(GameOption::PadStyle)] == 4);
    CHECK(g.known[int(GameOption::Subtitles)] && g.known[int(GameOption::AutoAim)] && g.known[int(GameOption::MusicVolume)]);
    CHECK(g.value[int(GameOption::CrouchToggle)] == 0 && !g.known[int(GameOption::Crosshairs)]);
    CHECK(!g.known[int(GameOption::Vibration)] && !g.known[int(GameOption::Widescreen)]);
    // 1. Na partida (Input_Init -> psiInput_Init): o .ini vale por cima dos padrões do PS2.
    Input_Init();
    CHECK(PlayerSetting[0].style == 4 && DrawInfo[2] == 1 && PlayerSetting[0].autoaim == 0);
    CHECK(PlayerSetting[0].crouch_toggle == 0 && SFXMusicGetVolume() == 40);
    CHECK(PlayerSetting[0].unknown_08 == 1);  // sem a chave: o padrão do jogo
    // 3. Um perfil padrão (def_codename com outros valores) pega o que o .ini tem e só isso.
    def_codename[0x18] = 0;   // legendas
    def_codename[0x20] = 7;   // estilo
    def_codename[0x24] = 1;   // mira automática
    def_codename[0x29] = 0;   // mira sem mirar
    def_codename[0x28] = 0;   // vibração
    const uint32_t music = 90;
    std::memcpy(def_codename + 0x10, &music, 4);
    Menu_MapDefaultCodename(0, 0, 0xff);
    CHECK(PlayerSetting[0].style == 4 && DrawInfo[2] == 1 && PlayerSetting[0].autoaim == 0 && SFXMusicGetVolume() == 40);
    CHECK(PlayerSetting[0].unknown_08 == 0 && PlayerSetting[0].unknown_09 == 0);  // do perfil padrão
    // As legendas que Menu_WillRunMiniMission desliga (versão americana) também ficam as do .ini.
    LanguageToLoad = 6;
    Menu_WillRunMiniMission();
    CHECK(DrawInfo[2] == 1 && def_codename[0x18] == 0);
    // Sem um .ini para gravar (as capturas sem --config), mudar uma opção não muda o que o .ini lembra.
    nf::g_controls_persist = false;
    nf::set_game_option(GameOption::Crosshairs, 1);
    CHECK(!nf::game_options_sync() && !g.known[int(GameOption::Crosshairs)]);
    // 4. Com ele: só a opção mudada passa a ser a do .ini (o arquivo deste teste não tem caminho: nada é escrito).
    nf::g_controls_persist = true;
    nf::set_game_option(GameOption::Widescreen, 1);  // só guardada: não vai para o .ini
    CHECK(!nf::game_options_sync());
    nf::set_game_option(GameOption::HudAlwaysOn, 0);
    nf::set_game_option(GameOption::InvertLook, 1);
    CHECK(nf::game_options_sync());
    CHECK(g.known[int(GameOption::HudAlwaysOn)] && g.value[int(GameOption::HudAlwaysOn)] == 0);
    CHECK(g.known[int(GameOption::InvertLook)] && nf::controls().pad_invert_y);
    CHECK(!g.known[int(GameOption::Widescreen)] && !g.known[int(GameOption::Crosshairs)]);
    CHECK(!nf::game_options_sync());  // nada mudou desde então
    // 2. Um codinome do cartão lido põe as opções dele e o .ini não muda.
    nf::set_game_option(GameOption::AutoSwitch, 0);
    nf::set_game_option(GameOption::PadStyle, 2);
    nf::g_controls_persist = false;
    nf::game_options_sync();
    CHECK(save_profile("CARD") == 7);
    nf::set_game_option(GameOption::AutoSwitch, 1);
    nf::set_game_option(GameOption::PadStyle, 4);
    nf::game_options_sync();
    nf::g_controls_persist = true;
    CHECK(load_profile("CARD") == 7);
    CHECK(PlayerSetting[0].auto_switch == 0 && PlayerSetting[0].style == 2);
    CHECK(!nf::game_options_sync());
    CHECK(!g.known[int(GameOption::AutoSwitch)] && g.value[int(GameOption::PadStyle)] == 4 && nf::controls().pad_style == 4);
    // O que vai para o arquivo: as chaves conhecidas de [controls], [ui] e [game], e só elas (a vibração, que ninguém
    // mudou, não é escrita).
    nf::Settings out;
    nf::save_game_options(out, g);
    CHECK(out.get("ui", "subtitles") == "1" && out.get("game", "crouch") == "hold");
    CHECK(out.get_int("game", "music_volume", -1) == 40 && out.get("game", "hud_always_on") == "0");
    CHECK(!out.has("game", "crosshairs") && !out.has("game", "widescreen") && !out.has("controls", "vibration"));
    CHECK(out.get_int("controls", "pad_style", -1) == 4 && out.get_bool("controls", "pad_invert_y", false));
    nf::g_controls_persist = false;
    nf::game_options_debug_power_on();
    nf::controls() = saved_controls;
}

TEST(options_ini_pad_style_only_when_the_player_changes_it) {
    OptWorld w;
    using nf::GameOption;
    const nf::ControlsConfig saved_controls = nf::controls();
    const nf::Settings saved_settings = nf::settings();
    const float saved_mouse = psiMouseSensitivity;
    psiMouseSensitivity = nf::kDefaultMouseSensitivity;
    // Os dois perfis padrão do disco: o esquema 7 (Classic Bond).
    def_codename[0x20] = 7;
    def_codename[0x38 + 0x20] = 7;
    // Um nightfire.ini novo, gravado de verdade (só na memória: sem caminho).
    nf::settings() = nf::Settings{};
    nf::controls() = nf::ControlsConfig{};
    nf::game_options_debug_power_on();
    nf::load_controls(nf::settings(), &nf::controls());
    nf::load_game_options(nf::settings(), &nf::game_options_ini());
    nf::g_controls_persist = true;
    Input_Init();  // psiInput_Init: o esquema do boot (0) e a referência das mudanças
    CHECK(PlayerSetting[0].style == 0);
    // Mudar a sensibilidade do mouse (a tela Modern) grava ela e nada das opções do jogo em vigor.
    {
        nf::ui::OptionsScreen s;
        s.open(nf::ui::Cat::Controls);
        s.command(nf::ui::OptionsScreen::Cmd::Down);  // do aparelho à sensibilidade do mouse
        CHECK(s.items()[size_t(s.focus())].opt == nf::ui::Opt::MouseSens);
        s.command(nf::ui::OptionsScreen::Cmd::Right);
    }
    CHECK(nf::settings().has("controls", "mouse_sensitivity"));
    CHECK(!nf::settings().has("controls", "pad_style") && !nf::settings().has("controls", "vibration"));
    CHECK(!nf::settings().has("controls", "pad_invert_y") && !nf::settings().has("controls", "pad_deadzone"));
    CHECK(!nf::settings().has("bindings", "fire"));
    // Na próxima partida o .ini não conhece o esquema: "Play without saving" (perfil padrão 1) fica no 7.
    const auto restart = [&] {
        nf::Settings next;
        next.parse(nf::settings().text());
        nf::settings() = next;
        nf::controls() = nf::ControlsConfig{};
        nf::game_options_debug_power_on();
        nf::load_controls(nf::settings(), &nf::controls());
        nf::load_game_options(nf::settings(), &nf::game_options_ini());
        Input_Init();
    };
    restart();
    CHECK(!nf::game_options_ini().known[int(GameOption::PadStyle)]);
    Menu_MapDefaultCodename(0, 1, 0xff);
    CHECK(PlayerSetting[0].style == 7);
    CHECK(!nf::game_options_sync() && !nf::settings().has("controls", "pad_style"));  // aplicar um perfil não é mudar
    // O jogador muda o esquema na linha CONTROLLER > Button layout: agora vai para o .ini.
    {
        nf::ui::OptionsScreen s;
        s.open(nf::ui::Cat::Controller);
        for (int i = 0; i < 8 && s.items()[size_t(s.focus())].opt != nf::ui::Opt::ButtonLayout; ++i) {
            s.command(nf::ui::OptionsScreen::Cmd::Down);
        }
        s.command(nf::ui::OptionsScreen::Cmd::Left);
    }
    CHECK(PlayerSetting[0].style == 6 && nf::settings().get_int("controls", "pad_style", -1) == 6);
    // E pelo jogo (a aba CONTROLS da pausa, "Controller Setup": Menu_ChangeControllerStyle), no tick seguinte.
    Menu_ChangeControllerStyle(0, 2);
    CHECK(nf::game_options_sync() && nf::settings().get_int("controls", "pad_style", -1) == 2);
    // Com a chave no .ini (também a de um arquivo antigo), o perfil padrão usa o esquema dele.
    restart();
    CHECK(nf::game_options_ini().known[int(GameOption::PadStyle)] && PlayerSetting[0].style == 2);
    Menu_MapDefaultCodename(0, 1, 0xff);
    CHECK(PlayerSetting[0].style == 2);
    nf::g_controls_persist = false;
    nf::game_options_debug_power_on();
    nf::controls() = saved_controls;
    nf::settings() = saved_settings;
    psiMouseSensitivity = saved_mouse;
}
