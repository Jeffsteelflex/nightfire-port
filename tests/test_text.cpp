// Testes da camada 2D: os textos do jogo (txt.cpp), as fontes (font.cpp) e as mensagens e legendas
// (text.cpp), com um arquivo de idioma e uma fonte montados à mão nos formatos do disco
// (docs/formats.md, "Arquivos de texto" e "Fontes").

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/font.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/pc_text.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/sprite.h"
#include "engine/sound.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"
#include "platform/psi_file.h"
#include "test_framework.h"

namespace {

void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int k = 0; k < 4; ++k) b.push_back(uint8_t(v >> (8 * k)));
}

// <idioma>Txt.dat: tamanho das strings, as strings, alinhamento (4 bytes a mais quando já alinhado),
// NumEntries, os deslocamentos das strings 1..N-1 (o primeiro é 0) e a tabela de rótulos.
std::vector<uint8_t> make_txt(const std::vector<std::string>& strings, const std::vector<uint32_t>& fixups) {
    std::vector<uint8_t> blob;
    std::vector<uint32_t> offsets;
    for (const std::string& s : strings) {
        offsets.push_back(uint32_t(blob.size()));
        blob.insert(blob.end(), s.begin(), s.end());
        blob.push_back(0);
    }
    std::vector<uint8_t> f;
    put32(f, uint32_t(blob.size()));
    f.insert(f.end(), blob.begin(), blob.end());
    for (uint32_t k = 0; k < ((4 - (blob.size() & 3)) & 0xffff); ++k) f.push_back(0xee);
    put32(f, uint32_t(strings.size() + 1));
    for (uint32_t o : offsets) put32(f, o);
    put32(f, uint32_t(fixups.size()));
    for (uint32_t x : fixups) put32(f, x);
    return f;
}

// Um FILES.BIN de um arquivo só (o de idioma), num arquivo temporário.
struct TempFiles {
    std::wstring path;
    explicit TempFiles(const std::vector<uint8_t>& txt) {
        path = (std::filesystem::temp_directory_path() / L"nf_test_text_files.bin").wstring();
        FILE* f = nullptr;
        _wfopen_s(&f, path.c_str(), L"wb");
        std::fwrite(txt.data(), 1, txt.size(), f);
        std::fclose(f);
        std::memset(FileList, 0, sizeof(FileList));
        std::memcpy(FileList[0].name, "USATxt.dat", 11);
        FileList[0].offset = 0;
        FileList[0].size = uint32_t(txt.size());
        psiFileSetup(path.c_str());
    }
    ~TempFiles() {
        psiFileShutdown();
        std::memset(FileList, 0, sizeof(FileList));
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

void fresh_heap() {
    psiMemHeapSize = 1u << 21;
    PtrHeap = nullptr;
    Mem_Init();
    txt_debug_power_on();
}

// Uma fonte com '!', 'A', 'B', 'M', 'T' e o ™ (0x99); 'A' tem dois pares de kerning em ordem crescente,
// como os do disco.
KERN_tag g_kern[2];
CHARDATA_tag g_chars[6];
FONT_tag g_font;
pstex_tag g_font_rec;
maptex_tag g_font_tex;
constexpr uint32_t kFontHash = 0x03000999;

void make_font() {
    font_debug_power_on();
    std::memset(FontTable, 0, sizeof(FontTable));
    g_kern[0] = {'A', 'B', -3, 0};
    g_kern[1] = {'A', 'C', -5, 0};
    const auto glyph = [](uint16_t code, uint16_t u, uint16_t w, int16_t pre, int16_t post) {
        CHARDATA_tag c{};
        c.code = code;
        c.u = u;
        c.v = 4;
        c.w = w;
        c.h = 10;
        c.pre = pre;
        c.post = post;
        return c;
    };
    g_chars[0] = glyph('!', 0, 4, 0, 1);
    g_chars[1] = glyph('A', 10, 10, 1, 2);
    g_chars[1].kern = g_kern;
    g_chars[1].kern_count = 2;
    g_chars[2] = glyph('B', 20, 8, 1, 1);
    g_chars[3] = glyph('M', 30, 12, 0, 2);
    g_chars[4] = glyph('T', 50, 8, 0, 1);
    g_chars[5] = glyph(0x99, 60, 16, 0, 0);
    g_font = FONT_tag{2, g_kern, 0x21, 0xff, 6, g_chars, 6.0f, 4.0f};
    FontTable[0] = {kFontHash, &g_font};
    FontTable[1] = {kFontHash, &g_font};
    hashtable_debug_power_on();
    hashtable_initialise();
    g_font_rec = pstex_tag{};
    g_font_rec.tex_index = 7;
    g_font_tex = maptex_tag{&g_font_rec, 256, 64};
    hashtable_additem(kFontHash, &g_font_tex);
}

char kFmt[] = "\xff\x01";

GPOINT extent_of(const char* text) {
    GPOINT e{};
    std::string t(text);
    Font_GetTextExtent(t.data(), kFmt, &e, nullptr, -1);
    return e;
}

}  // namespace

// ---- Txt ----

TEST(txt_loads_language_file_and_binds_labels) {
    fresh_heap();
    const TempFiles files(make_txt({"Hello", "Picked up %s", "Door"}, {0, 2}));
    Txt_SetLanguage(TXTLANG_USA);
    CHECK(Txt_GetLanguage() == TXTLANG_USA);
    CHECK(NumEntries == 4 && NumFixups == 2);
    CHECK(std::strcmp(Bank[0], "") == 0);
    CHECK(std::strcmp(Bank[1], "Hello") == 0 && std::strcmp(Bank[3], "Door") == 0);
    // Rótulo = tabela << 24 | índice: o índice final é FixupTable[tabela] + índice.
    CHECK(std::strcmp(Txt_BindLabel(0x00000002, 0), "Picked up %s") == 0);
    CHECK(std::strcmp(Txt_BindLabel(0x01000001, 0), "Door") == 0);
    CHECK(std::strcmp(Txt_BindLabel(0x01000005, 0), "Invalid Text Label") == 0);
    CHECK(Txt_BindLabel(0xffffffffu, 0) == nullptr);
    // Tabela além da FixupTable: o original leria fora dela; o port recusa e conta.
    CHECK(std::strcmp(Txt_BindLabel(0x02000000, 0), "Invalid Text Label") == 0 && g_txt_guards.bad_table == 1);
    // O índice 0 é um buffer novo do StringHeap (vazio).
    char* s = Txt_BindLabel(0, 0);
    CHECK(s == StringHeap[0] && s[0] == 0);
    // Recarregar (a cada fase) dá o mesmo resultado.
    Txt_LanguageInit(1);
    CHECK(std::strcmp(Txt_BindLabel(0x01000001, 0), "Door") == 0);
    txt_debug_power_on();
}

TEST(txt_pc_overrides_replace_ps2_hardware_texts) {
    // Só do port (engine/pc_text.h): os rótulos que falam do cartão de memória e do controle do PS2 dão o texto do PC;
    // os outros, e os que não têm o trecho esperado (outra versão do arquivo), passam direto; só com o USATxt.dat.
    txt_debug_power_on();
    static uint32_t fix[2] = {0, 0x3d0};
    static P32<char> bank[0x3d2];
    static char other[] = "Door";
    static char no_card[] = "No memory card (8MB) (for PlayStation\xae" "2) in MEMORY CARD slot %s.";
    static char space[] = "Insufficient space on memory card (8MB) (for PlayStation\xae" "2) in MEMORY CARD slot %s. "
                          "NightFire requires %dKB of free space to save data.";
    static char pad[] = "analog controller (DUALSHOCK\xae" "2) is not inserted into controller port %s";
    static char moved[] = "Save Successful";  // um rótulo da tabela com outra string: outra versão do arquivo
    for (P32<char>& b : bank) b = other;
    bank[0x26b] = no_card;
    bank[0x26a] = space;
    bank[0x3c9] = pad;
    bank[0x26c] = moved;
    FixupTable = fix;
    NumFixups = 2;
    Bank = bank;
    NumEntries = 0x3d2;
    SelLang = TXTLANG_USA;
    const std::string lang = PcText_Language();
    PcText_SetLanguage("en");
    CHECK(std::strcmp(Txt_BindLabel(0x26b, 0), "The save folder could not be found.") == 0);
    CHECK(Txt_BindLabel(0x26b, 0) == Txt_BindLabel(0x26b, 0));  // o ponteiro é fixo (os menus o guardam)
    CHECK(Txt_BindLabel(0x100, 0) == other);                    // sem troca: a string do jogo
    CHECK(Txt_BindLabel(0x26c, 0) == moved);                    // a string não fala do cartão: passa direto
    // O texto do PC no lugar do formato do jogo, com os argumentos do original (o slot e os KB).
    char out[256];
    std::snprintf(out, sizeof(out), Txt_BindLabel(0x26a, 0), "1", 0x52);
    CHECK(std::strcmp(out, "Not enough free disk space. NightFire needs 82 KB free to save data.") == 0);
    std::snprintf(out, sizeof(out), Txt_BindLabel(0x3c9, 0), "1");
    CHECK(std::strcmp(out, "Controller 1 is disconnected.") == 0);
    // Em português (sem as fontes carregadas, os acentos viram a letra sem acento, como PcText_Get).
    PcText_SetLanguage("pt");
    CHECK(PcText_ToGame("A pasta dos dados gravados não foi encontrada.") == Txt_BindLabel(0x26b, 0));
    // Outro idioma carregado: o texto do arquivo.
    SelLang = TXTLANG_FRENCH;
    CHECK(Txt_BindLabel(0x26b, 0) == no_card);
    PcText_SetLanguage(lang);
    txt_debug_power_on();
}

TEST(txt_pc_override_table_is_safe) {
    // Cada linha: um rótulo só uma vez, o trecho e os dois idiomas, e as mesmas conversões (%s, %d) em inglês e em
    // português; a trava do formato recusa um texto do PC que consumiria mais argumentos que o do jogo.
    std::vector<uint32_t> seen;
    for (size_t i = 0; i < PcText_LabelOverrideCount(); ++i) {
        const PcLabelOverride& o = PcText_LabelOverrideAt(i);
        for (uint32_t s : seen) CHECK(s != o.label);
        seen.push_back(o.label);
        CHECK(o.match != nullptr && o.match[0] != 0 && o.pt != nullptr && o.en != nullptr);
        CHECK(PcText_FormatPrefix(o.en, o.pt) && PcText_FormatPrefix(o.pt, o.en));
        CHECK(std::strstr(o.en, "memory card") == nullptr && std::strstr(o.en, "PlayStation") == nullptr);
    }
    CHECK(seen.size() >= 20);
    CHECK(PcText_FormatPrefix("Free%.0s %d KB", "slot %s needs %dKB"));
    CHECK(PcText_FormatPrefix("", "slot %s"));
    CHECK(PcText_FormatPrefix("100%% done", ""));
    CHECK(!PcText_FormatPrefix("%d KB", "slot %s"));
    CHECK(!PcText_FormatPrefix("%s %s", "slot %s"));
    CHECK(!PcText_FormatPrefix("%*d", "%d"));  // a largura '*' também consome um argumento
    // Um texto do PC que não cabe no formato do jogo fica de fora (a string do jogo vale).
    CHECK(PcText_GameLabel(0x26a, "memory card slot %s") == nullptr);
    CHECK(PcText_GameLabel(0x26a, "memory card slot %s, %dKB") != nullptr);
    CHECK(PcText_GameLabel(0x12345, "memory card") == nullptr);
}

TEST(txt_string_heap_rotates_and_respects_locks) {
    txt_debug_power_on();
    char* a = Txt_GetStringFromHeap(0);
    char* b = Txt_GetStringFromHeap(0);
    CHECK(a == StringHeap[0] && b == StringHeap[1]);
    Txt_LockString(b);
    Txt_LockString(b);
    CHECK(StringHeapLock[1] == 2);
    Txt_UnlockString(b);
    CHECK(StringHeapLock[1] == 1);
    // Destravado, um buffer volta depois de dar a volta; o travado é pulado.
    for (int i = 2; i < kStringHeapCount; ++i) Txt_GetStringFromHeap(0);
    CHECK(Txt_GetStringFromHeap(0) == StringHeap[0]);
    CHECK(Txt_GetStringFromHeap(0) == StringHeap[2]);
    // Strings fora do heap não são contadas.
    char outside[8] = "x";
    Txt_LockString(outside);
    Txt_UnlockString(nullptr);
    // Tudo travado: o buffer de reserva (fora do heap).
    for (int i = 0; i < kStringHeapCount; ++i) StringHeapLock[i] = 1;
    char* spare = Txt_GetStringFromHeap(0);
    CHECK(spare != nullptr && (spare < &StringHeap[0][0] || spare > &StringHeap[kStringHeapCount - 1][0]));
    txt_debug_power_on();
}

// ---- Font ----

TEST(font_parse_format_codes) {
    make_font();
    uint32_t flags = 0x1f0;
    GPOINT scale{1.0f, 1.0f};
    char centre[] = "\xff\x02\xfe\x03";
    Font_ParseFormat(centre, flags, scale);
    CHECK(flags == 0x102);  // alinhamento: zera o byte baixo e põe 2 (centrado); a flag 1 (só medir) não estava
    CHECK(Font == &g_font && CharInfo.texture == nf::p32_u(&g_font_rec));
    CHECK(CharInfo.inv_w == 1.0f / 256.0f && CharInfo.inv_h == 1.0f / 64.0f);
    char right[] = "\xfa\x10\x20\x30\x00\xfe\x02";
    flags = 0;
    Font_ParseFormat(right, flags, scale);
    CHECK(flags == 4 && CharInfo.r == 0x10 && CharInfo.g == 0x20 && CharInfo.b == 0x30);
    // Com a flag 1 (só medir), a troca de fonte não mexe na textura.
    CharInfo.texture = 0;
    flags = 1;
    Font_ParseFormat(centre, flags, scale);
    CHECK(CharInfo.texture == 0);
    CHECK(Font_GetAlignment(centre) == 3 && Font_GetAlignment(kFmt) == 1);
    char colour_then_align[] = "\xfa\x01\x02\x03\x04\xfe\x02";
    CHECK(Font_GetAlignment(colour_then_align) == 2);
    hashtable_debug_power_on();
}

TEST(font_text_extent_with_kerning_and_quirks) {
    make_font();
    // "AB": A (sem anterior: sem espaço antes) anda 2 + 10; B ajusta -3 pelo par (A, B), anda 1 antes,
    // 1 depois e 8: 19. Altura: o primeiro glifo (10) + espaço entre linhas (6) + 1.
    GPOINT e = extent_of("AB");
    CHECK(e.x == 19.0f && e.y == 17.0f);
    // "AC": C não existe e cai no glifo anterior do buraco (B); o par (A, C) existe, mas a lista para no
    // primeiro par menor que C, como no original: sem kerning.
    e = extent_of("AC");
    CHECK(e.x == 22.0f);
    // O espaço anda 4 * 1.25 mas não conta na largura; '\n' desce uma linha.
    CHECK(extent_of("A ").x == 12.0f);
    e = extent_of("A\nB");
    CHECK(e.x == 12.0f && e.y == 34.0f);
    // ™: T e M em meio tamanho (4 + 1 e 6 + 2).
    CHECK(extent_of("\x99").x == 13.0f);
    // 0x92 vira apóstrofo (sem glifo aqui: cai no anterior ao buraco, '!').
    CHECK(extent_of("\x92").x == 5.0f);
    // Quantos cabem: A termina em 12, B em 19.
    char abab[] = "ABAB";
    CHECK(Font_GetTextCountExtent(abab, kFmt, nullptr, 15.0f) == 1);
    CHECK(Font_GetTextCountExtent(abab, kFmt, nullptr, 20.0f) == 2);
    // Escala 2 em x: tudo dobra (o kerning também).
    GPOINT scale{2.0f, 1.0f};
    std::string t("AB");
    Font_GetTextExtent(t.data(), kFmt, &e, &scale, -1);
    CHECK(e.x == 38.0f);
    // Só "count" caracteres.
    Font_GetTextExtent(t.data(), kFmt, &e, nullptr, 1);
    CHECK(e.x == 12.0f);
    hashtable_debug_power_on();
}

TEST(font_word_wrap_breaks_after_last_space) {
    make_font();
    txt_debug_power_on();
    // Na quebra de linha a largura de cada glifo é kern + antes + depois + largura + 1 (sem escala):
    // A 14, B 8 a mais (22), o segundo A passa de 30.
    std::string text("AB AB AB");
    char* cursor = text.data();
    char* line = Font_WordWrapString(&cursor, uint16_t(text.size()), kFmt, 30, nullptr);
    CHECK(line != nullptr && std::strcmp(line, "AB ") == 0 && cursor == text.data() + 3);
    line = Font_WordWrapString(&cursor, 0, kFmt, 30, nullptr);
    CHECK(std::strcmp(line, "AB ") == 0 && cursor == text.data() + 6);
    line = Font_WordWrapString(&cursor, 0, kFmt, 30, nullptr);
    CHECK(std::strcmp(line, "AB") == 0 && cursor == nullptr);
    // Sem espaço: corta antes do caractere que passou.
    std::string word("ABABAB");
    cursor = word.data();
    line = Font_WordWrapString(&cursor, 0, kFmt, 30, nullptr);
    CHECK(std::strcmp(line, "AB") == 0 && cursor == word.data() + 2);
    // '\n' termina a linha e fica nela.
    std::string two("A\nB");
    cursor = two.data();
    line = Font_WordWrapString(&cursor, 0, kFmt, 100, nullptr);
    CHECK(std::strcmp(line, "A\n") == 0 && cursor == two.data() + 2);
    hashtable_debug_power_on();
}

// ---- Text ----

namespace {
// Heap, hashtable, fonte, sprites, idioma e as câmeras 0 a 5 (todas desenhando sprites).
struct TextWorld {
    alignas(16) viewer_tag viewers[6]{};
    TextWorld() {
        fresh_heap();
        make_font();
        sprite_debug_power_on();
        text_debug_power_on();
        std::memset(&GameState, 0, sizeof(GameState));
        std::memset(MPSettings, 0, sizeof(MPSettings));
        GS_SetRefreshRate(60, 60);
        for (int i = 0; i < 6; ++i) {
            viewers[i].sprites = 1;
            glb_viewer[i] = &viewers[i];
        }
        DrawInfo[2] = 1;
        Sprite_InitLists();
        Text_Init(0);
    }
    ~TextWorld() {
        for (viewer_tag*& v : glb_viewer) v = nullptr;
        DrawInfo[2] = 0;
        text_debug_power_on();
        sprite_debug_power_on();
        hashtable_debug_power_on();
    }
};
char* heap_text(const char* s) {
    char* b = Txt_GetStringFromHeap(0);
    std::memcpy(b, s, std::strlen(s) + 1);
    return b;
}
}  // namespace

TEST(text_add_msg_queues_lines_by_priority_and_time) {
    const TextWorld w;
    // A mensagem cabe numa linha (largura 480): uma linha, tempo mínimo de 60 ticks fora das legendas.
    Text_AddMsg(0, 2, 1, heap_text("AB"), 0, 10);
    Text_AddMsg(0, 1, 1, heap_text("BA"), 0, 200);
    Text_AddMsg(0, 2, 6, heap_text("A"), 0, 120);
    auto* m = reinterpret_cast<TXT_MSG*>(nf::raw(MsgQueue.used.first));
    // Ordem de prioridade crescente; iguais na ordem de chegada.
    CHECK(m != nullptr && std::strcmp(m->text, "BA") == 0 && m->time == 200.0f && m->priority == 1);
    m = reinterpret_cast<TXT_MSG*>(nf::raw(m->node.next));
    CHECK(m != nullptr && std::strcmp(m->text, "AB") == 0 && m->time == 60.0f && m->type == 1 && m->player == 0);
    m = reinterpret_cast<TXT_MSG*>(nf::raw(m->node.next));
    CHECK(m != nullptr && m->type == 6 && m->time == 120.0f);
    // A linha fica travada no StringHeap.
    CHECK(StringHeapLock[(m->text - &StringHeap[0][0]) / kStringHeapSize] == 1);
    // Text_UpdateMsg: a primeira do tipo, depois o tempo corre e ela sai.
    P32<TXT_MSG> cur = nullptr;
    CHECK(Text_UpdateMsg(0, 1, &cur) == 1 && cur != nullptr && std::strcmp(cur->text, "BA") == 0);
    for (int t = 0; t < 199; ++t) CHECK(Text_UpdateMsg(0, 1, &cur) == 0);
    CHECK(Text_UpdateMsg(0, 1, &cur) == 1 && cur != nullptr && std::strcmp(cur->text, "AB") == 0);
    // Câmera sem sprites: nada muda.
    glb_viewer[0]->sprites = 0;
    CHECK(Text_UpdateMsg(0, 1, &cur) == 0);
    CHECK(g_text_stats.added[1] == 2 && g_text_stats.added[6] == 1);
}

TEST(text_add_msg_splits_long_messages_and_shares_time) {
    const TextWorld w;
    TextMsgFormats[1].width = 30;  // cabe "AB " por linha (ver font_word_wrap_breaks_after_last_space)
    Text_AddMsg(0, 0, 1, heap_text("AB AB AB"), 0, 800);
    TextMsgFormats[1].width = 0x1e0;
    auto* a = reinterpret_cast<TXT_MSG*>(nf::raw(MsgQueue.used.first));
    CHECK(a != nullptr && a->next != nullptr && a->next->next != nullptr && a->next->next->next == nullptr);
    if (a == nullptr || a->next == nullptr || a->next->next == nullptr) return;
    TXT_MSG* const second = a->next;
    // Cada linha leva a parte do tempo pelo tamanho: 3/8, 3/8 e 2/8 de 800.
    CHECK(a->time == 300.0f && second->time == 300.0f && second->next->time == 200.0f);
    CHECK(std::strcmp(second->next->text, "AB") == 0);
    // As linhas seguem uma depois da outra na fila.
    CHECK(a->node.next == &second->node && second->node.next == &second->next->node);
    // A mensagem segue linha a linha no Text_UpdateMsg.
    P32<TXT_MSG> cur = nullptr;
    Text_UpdateMsg(0, 1, &cur);
    for (int t = 0; t < 300; ++t) Text_UpdateMsg(0, 1, &cur);
    CHECK(cur == second);
}

TEST(text_subtitles_need_the_option_and_show_on_sprites) {
    const TextWorld w;
    DrawInfo[2] = 0;
    Text_AddMsg(-1, 0, 4, heap_text("AB"), 0, 90);
    CHECK(MsgQueue.used.first == nullptr && g_text_stats.refused == 1);
    // Prioridade 0xff força a legenda mesmo desligada.
    Text_AddMsg(-1, 0xff, 4, heap_text("AB"), 0, 90);
    CHECK(MsgQueue.used.first != nullptr);
    // Sem cena (câmera 4 desligada, sem filme): vira a legenda de duas linhas (tipo 5).
    auto* m = reinterpret_cast<TXT_MSG*>(nf::raw(MsgQueue.used.first));
    CHECK(m->type == 5 && m->time == 90.0f);
    Text_Update2Line();
    CHECK(SubTitleMsg == m && SubTitleSpr1 != nullptr && SubTitleSpr1->priority == 0);
    CHECK(SubTitleSpr1->x == 0x100 && SubTitleSpr1->y == 0x192 && SubTitleSpr1->colour == 0x7d6d59ffu);
    CHECK(SubTitleSpr1->viewer == 5 && std::strcmp(SubTitleSpr1->text, "AB") == 0);
    CHECK(SubTitleSpr1->uw == 19);  // a largura medida, para centrar
    CHECK(SubTitleSpr2->priority == -1);
    // Em cena com câmera (4 ligada): a legenda de uma linha, no SubTitleSpr com o contorno.
    Text_FlushAllSubtitles();
    CHECK(SubTitleMsg == nullptr && MsgQueue.used.first == nullptr);
    glb_viewer[4]->active = 1;
    glb_viewer[5]->sprites = 0;
    DrawInfo[2] = 1;
    Text_AddMsg(-1, 0, 4, heap_text("BA"), 0, 30);
    Text_Update();
    CHECK(SubTitleMsg != nullptr && SubTitleSpr->priority == 0 && SubTitleSpr->viewer == 4);
    CHECK(SubTitleSpr->x == 0x100 && SubTitleSpr->y == 0x1aa && (SubTitleSpr->flags & SPRF_OUTLINE) != 0);
    for (int t = 0; t < 30; ++t) Text_Update();
    CHECK(SubTitleMsg == nullptr && SubTitleSpr->priority == -1);
}

TEST(text_speech_subtitles_from_sounds) {
    const TextWorld w;
    std::memset(Snd2Lbl, 0, sizeof(Snd2Lbl));
    std::memset(SFXOutputData, 0, sizeof(SFXOutputData));
    Snd2Lbl[3] = {1, 344};
    Snd2Lbl[4] = {0x80000001u, 345};
    const float seconds = 2.0f, range = 10.0f;
    std::memcpy(SFXOutputData + 344 * 0x18 + 0x10, &seconds, 4);
    std::memcpy(SFXOutputData + 345 * 0x18 + 0x10, &seconds, 4);
    g_fSubtitleDurationModification = 0.92f;
    static uint32_t fix[1] = {0};
    static P32<char> bank[2] = {nullptr, nullptr};
    static char line[] = "AB";
    bank[1] = line;
    FixupTable = fix;
    NumFixups = 1;
    Bank = bank;
    NumEntries = 2;
    // Legendas desligadas: só o rótulo com o bit 31.
    DrawInfo[2] = 0;
    Sound_DoSubtitle(344);
    CHECK(MsgQueue.used.first == nullptr);
    Sound_DoSubtitle(345);
    auto* m = reinterpret_cast<TXT_MSG*>(nf::raw(MsgQueue.used.first));
    // 0.92 * 2 s * 60 = 110 ticks, legenda (sem cena: tipo 5), prioridade 0.
    CHECK(m != nullptr && m->type == 5 && m->time == 110.0f && m->priority == 0 && m->text != nullptr &&
          std::strcmp(m->text, "AB") == 0);
    Text_FlushAllSubtitles();
    // Som que não está na tabela: nada.
    DrawInfo[2] = 1;
    Sound_DoSubtitle(999);
    CHECK(MsgQueue.used.first == nullptr);
    // O caminho do som (engine/sound.cpp): Sound_UpdateSounds começa o pedido e Sound_ReqestPlaySfx decide a
    // legenda. Com "3D de verdade" (+0x15), a mais da metade do alcance do ouvinte não tem legenda; perto, tem; 2D
    // sempre.
    std::memcpy(SFXOutputData + 344 * 0x18 + 8, &range, 4);
    SFXOutputData[344 * 0x18 + 0x15] = 1;
    SFXOutputData[344 * 0x18 + 0x14] = 1;
    sound_debug_power_on();
    Sound_Init();
    GlobalListenerPos = {0.0f, 0.0f, 0.0f, 1.0f};
    _VECTOR far_away{8.0f, 0.0f, 0.0f, 1.0f}, near{2.0f, 0.0f, 0.0f, 1.0f};
    CHECK(Sound_Play3D(344, &far_away, 100.0f, 0.0f, 0.0f, 0, 0, 0) != 0);
    Sound_UpdateSounds(&GlobalListenerPos);
    CHECK(MsgQueue.used.first == nullptr);
    Sound_Play3D(344, &near, 100.0f, 0.0f, 0.0f, 0, 0, 0);
    Sound_UpdateSounds(&GlobalListenerPos);
    CHECK(MsgQueue.used.first != nullptr);
    Text_FlushAllSubtitles();
    Sound_Play(344, 100.0f, 0, 0);
    Sound_UpdateSounds(&GlobalListenerPos);
    CHECK(MsgQueue.used.first != nullptr);
    sound_debug_power_on();
    std::memset(Snd2Lbl, 0, sizeof(Snd2Lbl));
    std::memset(SFXOutputData, 0, sizeof(SFXOutputData));
    txt_debug_power_on();
}

TEST(text_add_msg_without_queue_drops_the_message) {
    // Outros testes chamam código que manda mensagens sem Text_Init: a mensagem some, sem laço.
    text_debug_power_on();
    std::memset(&MsgQueue, 0, sizeof(MsgQueue));
    Text_AddMsg(0, 0, 1, nullptr, 7, 60);
    CHECK(MsgQueue.used.first == nullptr && g_text_stats.no_node == 1);
    text_debug_power_on();
}

TEST(text_received_messages_ring_and_list_insert) {
    int32_t buf[3];
    CIRBUF_INFO c{};
    CHECK(CirBuf_Init(&c, 3, buf) == 1 && c.size == 3 && buf[0] == 0);
    CHECK(CirBuf_Add(&c, 10) == 0 && CirBuf_Add(&c, 20) == 0 && CirBuf_Add(&c, 30) == 0);
    CHECK(c.count == 3 && c.head == 0);
    CHECK(CirBuf_Add(&c, 40) == 10);  // cheio: devolve o mais antigo
    CHECK(c.count == 3 && buf[0] == 40);
    // LList_InsertAt (de trás para a frente): depois do último com a mesma prioridade ou menor.
    TXT_MSG n[4]{};
    n[0].priority = 1;
    n[1].priority = 3;
    n[2].priority = 1;
    n[3].priority = 0;
    LLISTINFO_tag list{};
    int32_t CompMsgTxt(const void*, const void*);
    for (TXT_MSG& m : n) LList_InsertAt(&list, &m.node, 0, &CompMsgTxt);
    CHECK(list.first == &n[3].node && n[3].node.next == &n[0].node && n[0].node.next == &n[2].node);
    CHECK(n[2].node.next == &n[1].node && list.last == &n[1].node && list.count == 4);
}

TEST(text_box_keys_count_from_0xffff) {
    alignas(16) static M_TEXTBOX box;
    std::memset(&box, 0, sizeof(box));
    box.key = 0xffff;
    Text_SetupChar(&box, 'A', 0, 0, 0x19, 0x10, 0);
    Text_SetupChar(&box, 'B', 0x19, 0, 0x19, 0x10, 8);
    CHECK(box.key == 1 && box.keys[0].key == 'A' && box.keys[1].x == 0x19 && box.keys[1].blocked == 8);
    CHECK(sizeof(M_TEXTBOX) == 0x9b0 && offsetof(M_TEXTBOX, keys) == 0x478 && offsetof(M_TEXTBOX, key) == 0x9ac);
}
