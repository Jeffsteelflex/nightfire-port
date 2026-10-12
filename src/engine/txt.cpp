// Port do módulo Txt do ACTION.ELF (0x001caa10-0x001caeb4): os textos do jogo. Ver txt.h.
//
// Divergências:
// - Txt_BindLabel: um rótulo cuja tabela (byte alto) passa de NumFixups - 1 devolve "Invalid Text
//   Label" e conta em g_txt_guards.bad_table; o original leria a FixupTable além do fim (a entrada
//   NumFixups existe, mas nunca é escrita).
// - Txt_LoadLanguage: SelLang fora de FNames ou um arquivo menor que os tamanhos que ele declara param a
//   leitura (g_txt_guards); o original leria além da tabela ou do buffer.
// - Txt_BindLabel (só do port): com o USATxt.dat, os rótulos do hardware do PS2 (o cartão de memória, o controle
//   DUALSHOCK®2, o multitap) dão o texto do PC de engine/pc_text.h (PcText_GameLabel); no PC não há cartão nem portas.
// - O buffer de reserva (dontCrashString, devolvido com o StringHeap cheio) fica logo antes do StringHeap
//   no PS2, e Txt_LockString contaria um ponteiro para dentro dele como o buffer 0 (a divisão trunca para
//   zero); no port os dois não ficam colados.

#include "engine/txt.h"

#include <cstdio>
#include <cstring>

#include "engine/build.h"
#include "engine/memory.h"
#include "engine/pc_text.h"
#include "platform/psi.h"
#include "platform/psi_file.h"

int32_t SelLang = TXTLANG_UK;
int32_t LanguageToLoad = TXTLANG_UK;
P32<char>* Bank = nullptr;  // um vetor de palavras de 4 bytes no heap do jogo
char* BankData = nullptr;
uint32_t NumEntries = 0;
uint32_t NumFixups = 0;
uint32_t* FixupTable = nullptr;
char StringHeap[kStringHeapCount][kStringHeapSize];
uint8_t StringHeapLock[kStringHeapCount];
uint32_t StringHeapCnt = 0;
TxtGuards g_txt_guards;

// Os nomes de FNames (0x002c7498). O FILES.BIN tem UKTxt.dat, USATxt.dat etc.; psiFileOpen compara sem
// diferenciar maiúsculas.
const char* const FNames[kLanguageCount] = {"UKTxt.Dat", "FRTxt.Dat", "GRTxt.Dat",  "SPTxt.Dat", "ITTxt.Dat",
                                            "DUTxt.Dat", "USATxt.Dat", "JAPTxt.Dat", "SWTxt.Dat"};

namespace {
char kEmpty[1] = "";                       // 0x0030cef0
char kNotLoaded[] = "Not Loaded";          // Txt_BindLabel sem Bank
char kInvalidLabel[] = "Invalid Text Label";
char dontCrashString[kStringHeapSize];     // dontCrashString.51 (0x00324c00)

uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// Índice do buffer do StringHeap que contém s, como o original: (s - StringHeap) / 0x168 com sinal,
// comparado sem sinal com 0x100.
uint32_t heap_index(const char* s) {
    const int32_t d = int32_t(intptr_t(s) - intptr_t(&StringHeap[0][0]));
    return uint32_t(d / kStringHeapSize);
}
}  // namespace

void txt_debug_power_on() {
    SelLang = TXTLANG_UK;
    LanguageToLoad = TXTLANG_UK;
    Bank = nullptr;
    BankData = nullptr;
    NumEntries = 0;
    NumFixups = 0;
    FixupTable = nullptr;
    std::memset(StringHeap, 0, sizeof(StringHeap));
    std::memset(StringHeapLock, 0, sizeof(StringHeapLock));
    StringHeapCnt = 0;
    g_txt_guards = {};
}

// [decomp] 0x001caa10 Txt_GetStringFromHeap
// O próximo buffer destravado a partir de StringHeapCnt (depois, do começo); a trava dele passa a ser
// "lock" (0 nos chamadores: o buffer fica livre para ser entregue de novo depois de dar a volta). Com
// todos travados, o buffer de reserva.
char* Txt_GetStringFromHeap(int8_t lock) {
    uint32_t i = 0;
    bool found = false;
    if (uint16_t(StringHeapCnt) < 0x100) {
        for (i = StringHeapCnt & 0xffff; i < 0x100; ++i) {
            if (StringHeapLock[i] == 0) {
                found = true;
                break;
            }
        }
    }
    if (!found) {
        for (i = 0; i < StringHeapCnt; ++i) {
            if (StringHeapLock[i] == 0) {
                found = true;
                break;
            }
        }
    }
    if (!found) return dontCrashString;
    StringHeapLock[i] = uint8_t(lock);
    StringHeapCnt = i + 1;
    StringHeap[i][0] = 0;
    return StringHeap[i];
}

// [decomp] 0x001caad0 Txt_LoadLanguage
// <prefixo vazio><FNames[SelLang]> do FILES.BIN: uint32 tamanho das strings, as strings (alinhadas a 4;
// o original soma 4 - tamanho % 4, então 4 quando já alinhado), uint32 NumEntries, uma palavra pulada,
// os deslocamentos das strings 2 a NumEntries - 1 (Bank[1] começa em 0), NumFixups e a FixupTable. O
// arquivo carregado é liberado; as tabelas ficam no heap (a FixupTable anterior não é liberada).
void Txt_LoadLanguage() {
    NumEntries = 0;
    if (BankData != nullptr) Mem_Free(&BankData);
    if (Bank != nullptr) Mem_Free(&Bank);
    BankData = nullptr;
    if (SelLang < 0 || SelLang >= kLanguageCount) {
        ++g_txt_guards.short_file;
        return;
    }
    char name[0x400];
    std::snprintf(name, sizeof(name), "%s%s", "", FNames[SelLang]);
    uint32_t size = 0;
    void* file = psiFileLoad(name, 0x3604, &size);
    if (file == nullptr) return;
    const uint8_t* const begin = static_cast<const uint8_t*>(file);
    const uint8_t* const end = begin + ((size + 0x7ff) & ~0x7ffu);  // psiFileLoad lê setores inteiros
    uint8_t* p = static_cast<uint8_t*>(file);
    const auto fits = [&](const uint8_t* at, uint32_t bytes) { return at <= end && uint32_t(end - at) >= bytes; };
    const uint32_t bytes = BIN_GetDWord(&p);
    if (!fits(p, bytes + 8)) {
        ++g_txt_guards.short_file;
        Mem_Free(&file);
        return;
    }
    BankData = static_cast<char*>(Mem_Malloc(bytes, 0x3604, 0));
    amemcpy(BankData, p, bytes);
    p += bytes + ((4 - (bytes & 3)) & 0xffff);
    NumEntries = BIN_GetDWord(&p);
    p += 4;
    Bank = static_cast<P32<char>*>(Mem_Malloc(NumEntries << 2, 0x3604, 0));
    Bank[0] = kEmpty;
    uint32_t offset = 0;
    for (uint32_t i = 1; i < NumEntries; ++i) {
        if (!fits(p, 4)) {
            ++g_txt_guards.short_file;
            NumEntries = i;
            break;
        }
        Bank[i] = BankData + offset;
        offset = rd32(p);
        p += 4;
    }
    p -= 4;
    NumFixups = BIN_GetDWord(&p);
    if (!fits(p, NumFixups * 4)) {
        ++g_txt_guards.short_file;
        NumFixups = 0;
    }
    FixupTable = static_cast<uint32_t*>(Mem_Malloc((NumFixups + 1) * 4, 0x3604, 0));
    for (uint32_t i = 0; i < NumFixups; ++i) FixupTable[i] = BIN_GetDWord(&p);
    Mem_Free(&file);
}

// [decomp] 0x001cad00 Txt_LanguageInit
// O parâmetro (partida a frio) não é usado: a cada fase os buffers são destravados e o idioma
// recarregado (o Mem_Init da carga apagou o heap; as tabelas antigas não são liberadas).
void Txt_LanguageInit(int8_t) {
    std::memset(StringHeapLock, 0, sizeof(StringHeapLock));
    StringHeapCnt = 0;
    BankData = nullptr;
    Bank = nullptr;
    Txt_LoadLanguage();
}

// [decomp] 0x001cad38 Txt_SetLanguage
void Txt_SetLanguage(int32_t language) {
    SelLang = language;
    Txt_LoadLanguage();
}

// [decomp] 0x001cad58 Txt_GetLanguage
int32_t Txt_GetLanguage() { return SelLang; }

// [decomp] 0x001cad60 Txt_BindLabel
// A string do rótulo: nulo para -1 ou sem idioma carregado, um buffer novo do StringHeap para o índice
// 0 (com a trava "lock"), "Invalid Text Label" fora da tabela, "Not Loaded" sem Bank.
// Só do port: com o texto americano carregado, os rótulos que falam do hardware do PS2 (o cartão de memória, o
// controle DUALSHOCK®2, o multitap) devolvem o texto do PC (PcText_GameLabel, engine/pc_text.h); o arquivo não muda.
char* Txt_BindLabel(uint32_t label, int8_t lock) {
    if (label == 0xffffffffu || FixupTable == nullptr) return nullptr;
    const uint32_t table = label >> 24;
    if (table >= NumFixups) {
        ++g_txt_guards.bad_table;
        return kInvalidLabel;
    }
    const uint32_t index = FixupTable[table] + (label & 0xffffff);
    if (index >= NumEntries) return kInvalidLabel;
    if (index == 0) return Txt_GetStringFromHeap(lock);
    if (Bank == nullptr) return kNotLoaded;
    if (SelLang == TXTLANG_USA) {
        if (const char* pc = PcText_GameLabel(label, Bank[index])) return const_cast<char*>(pc);
    }
    return Bank[index];
}

// [decomp] 0x001cae10 Txt_UnlockString
void Txt_UnlockString(char* s) {
    if (s == nullptr) return;
    const uint32_t i = heap_index(s);
    if (i < 0x100 && StringHeapLock[i] != 0) --StringHeapLock[i];
}

// [decomp] 0x001cae68 Txt_LockString
void Txt_LockString(char* s) {
    if (s == nullptr) return;
    const uint32_t i = heap_index(s);
    if (i < 0x100) ++StringHeapLock[i];
}
