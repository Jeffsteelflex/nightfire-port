// Testes do menu principal (o caminho de um jogador; engine/menu_frontend.h): o cartão do PC e o perfil gravado
// (ida e volta byte a byte, cartão com o perfil estragado ou sem ele, a pasta do disco), o teclado da tela do codinome
// (digitar, apagar, o máximo de 8 letras, o nome que não vale, o "End" que grava) e a dificuldade e o mapa das missões
// até ResetMap_LevelToLoad, num arquivo de menu feito à mão como em test_modern_menus.cpp.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>

#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/loadsave.h"
#include "engine/memory.h"
#include "engine/menu.h"
#include "engine/menu_frontend.h"
#include "engine/menu_modern.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/player_stats.h"
#include "engine/resetmap.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_card.h"
#include "platform/psi_file.h"
#include "platform/psi_gfx.h"
#include "platform/psi_input.h"
#include "test_framework.h"

namespace {

// ---- arquivo de menu (docs/formats.md, "Menus (item tipo 8)") ----
struct MenuFile {
    std::vector<uint8_t> b;
    size_t page_size_at = 0;
    MenuFile() { dw(0xfffffffa); }
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
    void control(uint32_t id, uint8_t type, int16_t x, int16_t y, int16_t ww, int16_t h, uint32_t tag = 0) {
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
        dw(0);
        dw(0);
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

uint32_t ptr32(const void* p) { return uint32_t(reinterpret_cast<uintptr_t>(p)); }
uint32_t fourcc(const char* s) { return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 | uint32_t(uint8_t(s[3])) << 24; }

char kKeyA[] = "A";
char kKeyB[] = "B";

// Uma tabela de textos mínima: os nomes dos perfis padrão ("New Codename" 0x29b e "Default" 0x211); o resto vazio.
char kEmptyText[] = "";
char kNewCodename[] = "New Codename";
char kDefault[] = "Default";
char* g_bank[0x300];
uint32_t g_fixups[1] = {0};

// O mundo dos testes: o heap, a camada 2D, os menus, o cartão em memória (vazio), os blocos do perfil como o construtor
// estático do ELF os deixa, o ícone do cartão e as tabelas de pontuação com as linhas.
struct FrontWorld {
    MenuFile file;
    nf::MemoryCard card;
    std::vector<uint8_t> icon = std::vector<uint8_t>(0x128f8, 0x5a);
    FrontWorld() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        hashtable_debug_power_on();
        hashtable_initialise();
        txt_debug_power_on();
        for (char*& s : g_bank) s = kEmptyText;
        g_bank[0x29b] = kNewCodename;
        g_bank[0x211] = kDefault;
        Bank = g_bank;
        NumEntries = 0x300;
        FixupTable = g_fixups;
        NumFixups = 1;
        sprite_debug_power_on();
        Sprite_InitLists();
        text_debug_power_on();
        Text_Init(0);
        menu_debug_power_on();
        menu_handlers_debug_power_on();  // também o menu principal, o perfil e o cartão
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
        const uint32_t names[2] = {0x29b, 0x211};
        std::memcpy(def_codename, &names[0], 4);
        std::memcpy(def_codename + 0x38, &names[1], 4);
        // SaveIFFBlocks como o construtor estático do ELF a monta.
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
    }
    ~FrontWorld() {
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
        std::memset(difficulty, 0, sizeof(difficulty));
        std::memset(menu_bonus, 0, sizeof(menu_bonus));
        while (GameFlow_Depth() > 0) GameFlow_PopState();
    }
    void load() {
        file.finish();
        MenuManager_Load(0x8000002, file.b.data());
    }
    // Um tick dos menus como no Game_Run (sem teclas).
    void tick(int n = 1) {
        for (int i = 0; i < n; ++i) {
            std::memset(g_psi_keyboard_mouse.down, 0, sizeof(g_psi_keyboard_mouse.down));
            Input_Update();
            MenuManager_Update();
            MenuManager_Monitor();
        }
    }
    // Um botão do controle do PS2 por um tick (como o mouse e os menus Modern: Input_SetAction).
    void press(uint32_t action) {
        std::memset(g_psi_keyboard_mouse.down, 0, sizeof(g_psi_keyboard_mouse.down));
        Input_Update();
        Input_SetAction(0, action, kActionDown | kActionPressed);
        MenuManager_Update();
        MenuManager_Monitor();
    }
    M_CONTROL* control(uint32_t id, uint32_t tag) {
        M_PAGE* p = menu_manager[0].page;
        for (M_CONTROL* c = p != nullptr ? static_cast<M_CONTROL*>(ll_first(p->controls)) : nullptr; c != nullptr; c = c->next) {
            if (c->id == id && c->tag == tag) return c;
        }
        return nullptr;
    }
};

// A caixa de respostas do cartão (0x4000002a) e a caixa de mensagem (0x4000002f), como no 08000002.
void card_box_pages(MenuFile& f) {
    f.page(0x4000002a);
    f.control(0x40000029, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x100000fc, MCT_LIST, 50, 296, 540, 92);
    f.control(0x100000fd, MCT_BUTTON, 50, 171, 540, 121);
    f.control(0x100000fb, MCT_MEMO, 59, 181, 523, 103);
    f.page(0x4000002f);
    f.control(0x4000002f, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x1000011e, MCT_BUTTON, 50, 171, 540, 121);
    f.control(0x1000011f, MCT_MEMO, 58, 181, 524, 103);
    f.control(0x10000120, MCT_LIST, 35, 296, 570, 92);
}

// Responde a pergunta do cartão que estiver aberta (PS2CardFlow.message) com `answer` (C_LBERROPTIONS_Handler).
void answer_card(uint32_t message, uint8_t answer) {
    if (PS2CardFlow.message != message || PS2CardFlow.answered != 0) return;
    PS2CardFlow.answer = answer;
    PS2CardFlow.answered = 1;
}

// LS_Save/LS_Load com o cartão, um quadro por volta, até o estado final (7 ou 8); responde "sim" ao sobrescrever.
uint16_t save_profile(const char* name) {
    char buf[0x20];
    std::snprintf(buf, sizeof(buf), "%s", name);
    LS_FlushStates();
    for (int i = 0; i < 400 && Save_State != 7 && Save_State != 8; ++i) {
        LS_Save(buf, 0, 0xff);
        PS2PollMemoryCard();
        answer_card(4, 2);
    }
    return Save_State;
}

uint16_t load_profile(const char* name, uint32_t error_message = 0, uint8_t error_answer = 0) {
    char buf[0x20];
    std::snprintf(buf, sizeof(buf), "%s", name);
    LS_FlushStates();
    for (int i = 0; i < 400 && Load_State != 7 && Load_State != 8; ++i) {
        LS_Load(buf, 0, 0xff);
        PS2PollMemoryCard();
        if (error_message != 0) answer_card(error_message, error_answer);
    }
    return Load_State;
}

// O estado que o perfil grava, com valores reconhecíveis.
void set_profile_state() {
    PlayerSetting[0].invert_look = 1;
    PlayerSetting[0].style = 5;
    PlayerSetting[0].unknown_10 = 3;
    PlayerSetting[0].autoaim = 1;
    PlayerSetting[0].crouch_toggle = 1;
    PlayerSetting[0].unknown_0c = 2;
    const uint64_t bonus = 0x0123456789abcdefull;
    std::memcpy(menu_bonus, &bonus, 8);
    reinterpret_cast<M_ITEM*>(sp_level)[2].enabled = 1;
    reinterpret_cast<M_ITEM*>(sp_level)[0].enabled = 1;
    for (uint32_t i = 0; i < kScoringTableCount; ++i) {
        ScoringTable[i].best = 1000 * i + 7;
        ScoringTable[i].rows[0].actual = i % 16;
    }
    DrawInfo[0] = 1;
    DrawInfo[1] = 1;
    DrawInfo[2] = 1;
    const uint32_t on = 1;
    std::memcpy(CheatInfo + 4, &on, 4);
    GameState.unknown_52 = 1;
}

void clear_profile_state() {
    PlayerSetting[0] = PlayerInput_tag{};
    std::memset(menu_bonus, 0, sizeof(menu_bonus));
    std::memset(sp_level, 0, sizeof(sp_level));
    for (uint32_t i = 0; i < kScoringTableCount; ++i) {
        ScoringTable[i].best = 0;
        ScoringTable[i].rows[0].actual = 0;
    }
    DrawInfo[0] = DrawInfo[1] = DrawInfo[2] = 0;
    std::memset(CheatInfo, 0, sizeof(CheatInfo));
    GameState.unknown_52 = 0;
}

std::vector<uint8_t> card_file(const nf::PcCard& c, const char* path) {
    std::vector<uint8_t> out;
    c.read(path, &out);
    return out;
}

}  // namespace

TEST(card_profile_save_load_round_trip_is_byte_identical) {
    FrontWorld w;
    set_profile_state();
    CHECK(save_profile("BOND") == 7);
    CHECK(PS2SaveGameFine == 1);
    // A pasta como no cartão do PS2: o ícone do pacote, o icon.sys (com "PS2D") e os dados com o crc no fim.
    const std::vector<uint8_t> first = card_file(w.card, "BASLUS-20579BOND/BASLUS-20579BOND");
    CHECK(card_file(w.card, "BASLUS-20579BOND/007.ICN") == w.icon);
    const std::vector<uint8_t> sys = card_file(w.card, "BASLUS-20579BOND/icon.sys");
    CHECK(sys.size() == 0x3c4 && std::memcmp(sys.data(), "PS2D", 4) == 0);
    CHECK(first.size() > 8 && first.size() < 0x1000);
    uint32_t crc = 0;
    if (first.size() >= 4) std::memcpy(&crc, first.data() + first.size() - 4, 4);
    CHECK(crc == crc32buf(reinterpret_cast<const char*>(first.data()), uint32_t(first.size() - 4)));
    // O primeiro bloco é o das opções do jogador.
    CHECK(first.size() >= 4 && std::memcmp(first.data(), "PLRS", 4) == 0);

    // Lido de volta num estado zerado: tudo volta.
    clear_profile_state();
    CHECK(load_profile("BOND") == 7);
    CHECK(PlayerSetting[0].invert_look == 1 && PlayerSetting[0].style == 5 && PlayerSetting[0].unknown_10 == 3);
    CHECK(PlayerSetting[0].autoaim == 1 && PlayerSetting[0].crouch_toggle == 1 && PlayerSetting[0].unknown_0c == 2);
    CHECK(Menu_GetBonus(0) == 0x0123456789abcdefull);
    CHECK(reinterpret_cast<M_ITEM*>(sp_level)[2].enabled == 1 && reinterpret_cast<M_ITEM*>(sp_level)[3].enabled == 0);
    CHECK(ScoringTable[5].best == 5007 && ScoringTable[11].rows[0].actual == 11);
    CHECK(DrawInfo[0] == 1 && DrawInfo[1] == 1 && DrawInfo[2] == 1 && GameState.unknown_52 == 1);
    uint32_t cheat = 0;
    std::memcpy(&cheat, CheatInfo + 4, 4);
    CHECK(cheat == 1);

    // Gravado de novo (o cartão pergunta se sobrescreve; sim): os mesmos bytes.
    CHECK(save_profile("BOND") == 7);
    CHECK(card_file(w.card, "BASLUS-20579BOND/BASLUS-20579BOND") == first);
    CHECK(w.card.list("BASLUS-20579BOND").size() == 3);
}

TEST(card_profile_corrupt_or_missing) {
    FrontWorld w;
    set_profile_state();
    CHECK(save_profile("BOND") == 7);
    // Um byte trocado no arquivo: o crc não confere, "dados corrompidos" (6) com OK, e a leitura falha.
    std::vector<uint8_t>& data = w.card.files["BASLUS-20579BOND/BASLUS-20579BOND"];
    CHECK(data.size() > 20);
    if (data.size() > 20) data[20] ^= 0x40;
    clear_profile_state();
    CHECK(load_profile("BOND", 6, 1) == 8);
    CHECK(PlayerSetting[0].style == 0);  // nada carregado
    // Um perfil que não está no cartão: "falha ao ler" (7), cancelar.
    CHECK(load_profile("NOBODY", 7, 4) == 8);
    // Um cartão vazio: a listagem só tem os dois perfis padrão.
    w.card.clear();
    MENU_LS& l = menu_ls();
    l.op = 2;
    l.stale = 1;
    Menu_FlushEnum();
    uint32_t state = 0;
    for (int i = 0; i < 100 && state != 7; ++i) {
        state = Menu_EnumSaves(0, 0);
        PS2PollMemoryCard();
    }
    CHECK(state == 7 && l.count == 2);
    CHECK(std::strcmp(codename_buf[0], "New Codename") == 0 && std::strcmp(codename_buf[1], "Default") == 0);
}

TEST(card_folder_on_disk_and_enumeration) {
    FrontWorld w;
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring root = std::wstring(tmp) + L"nf_test_saves_" + std::to_wstring(GetCurrentProcessId());
    nf::FolderCard folder(root);
    nf::set_card(&folder);
    set_profile_state();
    CHECK(save_profile("ZED") == 7);
    CHECK(save_profile("ALPHA") == 7);
    std::vector<uint8_t> bytes;
    CHECK(folder.read("BASLUS-20579ZED/BASLUS-20579ZED", &bytes) && bytes.size() > 8);
    // A listagem do jogo: os dois padrões e os do cartão em ordem.
    MENU_LS& l = menu_ls();
    l.op = 2;
    l.stale = 1;
    Menu_FlushEnum();
    uint32_t state = 0;
    for (int i = 0; i < 100 && state != 7; ++i) {
        state = Menu_EnumSaves(0, 0);
        PS2PollMemoryCard();
    }
    CHECK(state == 7 && l.count == 4);
    CHECK(std::strcmp(codename_buf[2], "ALPHA") == 0 && std::strcmp(codename_buf[3], "ZED") == 0);
    // Apagar pelo jogo (com a pergunta, "apagar"): a pasta some.
    std::snprintf(l.name, sizeof(l.name), "ZED");
    char name[] = "ZED";
    psiDeleteData(name);
    for (int i = 0; i < 100 && psiDeletingData() != 4; ++i) {
        PS2PollMemoryCard();
        answer_card(0xd, 0x20);
    }
    bool dir = false;
    CHECK(!folder.exists("BASLUS-20579ZED", &dir));
    folder.clear();
    RemoveDirectoryW(root.c_str());
    nf::set_card(&w.card);
}

TEST(codename_keyboard_typing_delete_and_max_length) {
    FrontWorld w;
    MenuFile& f = w.file;
    f.page(0x40000020);
    f.control(0x40000011, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x10000074, MCT_BUTTON, 120, 201, 32, 32, 10);
    f.msg(0x18, ptr32(kKeyA));
    f.control(0x10000074, MCT_BUTTON, 160, 201, 32, 32, 11);
    f.msg(0x18, ptr32(kKeyB));
    f.control(0x10000074, MCT_BUTTON, 204, 323, 25, 16, 1000);
    f.control(0x10000074, MCT_BUTTON, 290, 323, 60, 16, 1001);
    f.control(0x10000074, MCT_BUTTON, 404, 319, 25, 24, 0x3ea);
    f.control(0x10000242, MCT_LABEL, 50, 40, 540, 17);
    f.control(0x10000075, MCT_LABEL, 117, 135, 406, 16);
    card_box_pages(f);
    w.load();
    MenuManager_Create(0x80000002u, 0x40000020, -1, 0, 0x28, 0, 5);
    w.tick();
    // O nome sugerido e o foco no "End"; a primeira tecla apaga o sugerido.
    CHECK(std::strcmp(codename_entry, "BOND") == 0 && cheat_mode == 0);
    CHECK(menu_manager[0].page->focus == w.control(0x10000074, 0x3ea));
    M_CONTROL* a = w.control(0x10000074, 10);
    M_CONTROL* b = w.control(0x10000074, 11);
    M_CONTROL* del = w.control(0x10000074, 1000);
    M_CONTROL* space = w.control(0x10000074, 1001);
    M_CONTROL* end = w.control(0x10000074, 0x3ea);
    CHECK(a != nullptr && b != nullptr && del != nullptr && space != nullptr && end != nullptr);
    if (a == nullptr || b == nullptr || del == nullptr || space == nullptr || end == nullptr) return;
    const auto key = [&](M_CONTROL* c) { C_KEYBOARD_Handler(0, c, c->id, 0x4b, 0, 0); };
    key(a);
    CHECK(std::strcmp(codename_entry, "A") == 0);
    for (int i = 0; i < 10; ++i) key(b);
    CHECK(std::strcmp(codename_entry, "ABBBBBBB") == 0);  // até 8 letras
    key(space);
    CHECK(std::strlen(codename_entry) == 8);
    key(del);
    CHECK(std::strcmp(codename_entry, "ABBBBBB") == 0);
    for (int i = 0; i < 12; ++i) key(del);  // vazio: nada a apagar
    CHECK(codename_entry[0] == 0);
    key(space);
    key(a);
    CHECK(std::strcmp(codename_entry, " A") == 0);
    // Os nomes que não valem: vazio ou só espaços (0x3bc), com o nome de um perfil padrão (0x2c0); os espaços das
    // pontas saem e o nome vira maiúsculas.
    char empty[16] = "";
    char spaces[16] = "   ";
    char clash[16] = "MYDEFAULT";
    char fine[16] = "  bond  ";
    CHECK(Menu_IsValidCodename(empty) == 0x3bc && Menu_IsValidCodename(spaces) == 0x3bc);
    CHECK(Menu_IsValidCodename(clash) == 0x2c0);
    CHECK(Menu_IsValidCodename(fine) == 0 && std::strcmp(fine, "BOND") == 0);
    // "End" com " A": vale (vira "A") e começa a gravar o perfil novo, que depois mostra o menu do codinome (com onde
    // gravar, como o menu principal deixa; sem, o jogo desiste).
    menu_ls().can_save = 1;
    key(end);
    const MENU_LS& l = menu_ls();
    CHECK(l.active == 1 && l.op == 1 && l.slot == 999 && std::strcmp(l.name, "A") == 0);
    CHECK(l.next_page == 0x4000001d && l.next_flags == 1);
    CHECK(Save_State == 2 && PSIOP_State == 1);
    // A caixa do cartão (0x4000002a) também chega ao estilo Modern como diálogo (as respostas na lista 0x100000fc; a
    // mensagem é a do memo, vazia com esta tabela de textos).
    bool card_dialog = false;
    for (int i = 0; i < 200 && w.card.files.count("BASLUS-20579A/BASLUS-20579A") == 0; ++i) {
        PS2PollMemoryCard();
        w.tick();
        const MmSnapshot snap = MenuModern_Snapshot();
        if (snap.page == kMmCardPage && snap.dialog.open && snap.dialog.box && snap.dialog.list == kMmCardList) {
            card_dialog = true;
        }
    }
    CHECK(w.card.files.count("BASLUS-20579A/BASLUS-20579A") == 1);
    CHECK(card_dialog);
    // Um nome que não vale abre a caixa do jogo (0x2c0) e a próxima tecla apaga o nome.
    std::snprintf(codename_entry, sizeof(codename_entry), "DEFAULT");
    menu_ls().active = 0;
    key(end);
    CHECK(del_when_keypressed == 1 && menu_ls().active == 0);
}

TEST(mission_map_difficulty_and_level_to_load) {
    FrontWorld w;
    MenuFile& f = w.file;
    // A dificuldade (0x40000023) e o mapa das missões (0x4000001c): a roda, os cinco rótulos, a imagem, a descrição e
    // o íris de cada uma.
    f.page(0x40000023);
    f.control(0x40000023, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x100000bf, MCT_SCROLL, 313, 48, 16, 32);
    for (uint32_t t = 0; t < 5; ++t) f.control(0x100000e6, MCT_LABEL, 323, int16_t(123 + 30 * t), 317, 17, t);
    f.control(0x100000be, MCT_LABEL, 114, 118, 150, 150);
    f.control(0x100001ee, MCT_MEMO, 50, 301, 540, 113);
    f.control(0x10000104, MCT_LABEL, 114, 118, 150, 150);
    f.page(0x4000001c);
    f.control(0x40000005, MCT_WINDOW, 0, 0, 640, 480);
    f.control(0x1000000e, MCT_SCROLL, 312, 48, 16, 32);
    for (uint32_t t = 0; t < 5; ++t) f.control(0x100000e3, MCT_LABEL, 323, int16_t(123 + 30 * t), 317, 17, t);
    f.control(0x10000020, MCT_LABEL, 114, 118, 150, 150);
    f.control(0x1000001f, MCT_MEMO, 50, 301, 540, 113);
    f.control(0x10000102, MCT_LABEL, 114, 118, 150, 150);
    f.control(0x1000010e, MCT_LABEL, 50, 419, 540, 21);
    f.control(0x1000022c, MCT_LABEL, 123, 232, 394, 16);
    card_box_pages(f);
    w.load();
    // As três dificuldades liberadas; as missões: 0 de carro e 1 a pé liberadas, 2 a pé bloqueada.
    for (uint32_t i = 0; i < 3; ++i) difficulty[i] = M_ITEM{0x30000a5, 0x14d + i, 0, i + 1, 1, {}, 0};
    M_ITEM* missions = reinterpret_cast<M_ITEM*>(sp_level);
    missions[0] = M_ITEM{0x3000085, 0, 0, 0x9000001, 1, {}, 0};
    missions[1] = M_ITEM{0x3000084, 0, 0, 0x7000005, 1, {}, 0};
    missions[2] = M_ITEM{0x30000b0, 0, 0, 0x7000001, 0, {}, 0};
    GameState.difficulty = 1;
    // A câmera 6 (o esmaecimento entre fases), que o GF_FADE_REQUEST usa.
    viewer_tag fade_view{};
    viewer_tag* saved_fade_view = glb_viewer[6];
    glb_viewer[6] = &fade_view;

    MenuManager_Create(0x80000002u, 0x40000023, -1, 0, 0x28, 0, 5);
    // O foco na roda (no disco, o script de entrada da janela).
    Manager_SendMessage(&menu_manager[0], 0x22, 0x100000bf, 0);
    w.tick(2);
    M_SCROLL* wheel = reinterpret_cast<M_SCROLL*>(w.control(0x100000bf, 0));
    CHECK(wheel != nullptr && wheel->min == 0 && wheel->max == 2);
    // Para baixo uma vez ("Agent") e escolher: a dificuldade 2 e, com o íris fechado, o mapa.
    __Menu_Send(0, 0x100000bf, 0x2e, 1, 0);
    w.press(0x1f);
    CHECK(GameState.difficulty == 2);
    w.tick(12);
    CHECK(Menu_CurrentPage(0) == 0x4000001c);
    Manager_SendMessage(&menu_manager[0], 0x22, 0x1000000e, 0);
    w.tick();
    // O mapa abre na última missão liberada; escolher a bloqueada não faz nada.
    M_SCROLL* map = reinterpret_cast<M_SCROLL*>(w.control(0x1000000e, 0));
    CHECK(map != nullptr && map->max == 0xb && map->pos == 1);
    __Menu_Send(0, 0x1000000e, 0x2e, 2, 0);
    w.press(0x1f);
    CHECK(MenuManager_Exists(0) != 0 && GameFlow_GetState() == GF_WAIT_START);
    // A missão liberada: ResetMap_LevelToLoad com o filme de abertura (071xxxxx), o fade e o menu fechado.
    __Menu_Send(0, 0x1000000e, 0x2e, 1, 0);
    w.press(0x1f);
    CHECK(MenuManager_Exists(0) == 0);
    CHECK(GameState.load_file == 0x7100005 && GameState.difficulty == 2);
    uint32_t multiplayer = 1;
    std::memcpy(&multiplayer, MPSettings + 0x180, 4);
    CHECK(multiplayer == 0);
    CHECK(GameFlow_GetState() == GF_FADE_WAIT);
    GameFlow_PopState();
    CHECK(GameFlow_GetState() == GF_LOAD_MAP);
    GameFlow_PopState();

    // Só no port: a missão de carro pulada (psiLaunchDriving) fica feita, libera a seguinte e volta ao mapa.
    missions[1].enabled = 0;
    CHECK(Menu_PortDrivingSkipped(0x7000005) == 0);
    CHECK(Menu_PortDrivingSkipped(0x9000001) == 1);
    CHECK(missions[0].enabled == 1 && missions[1].enabled == 1);
    CHECK(GameState.menu_page == 0x4000001c && GameState.load_file == 0x7000048 && GameFlow_GetState() == GF_LOAD_MAP);
    glb_viewer[6] = saved_fade_view;
}
