#pragma once
// Textos do jogo (módulo Txt do ACTION.ELF, 0x001caa10-0x001caeb4). Formato em docs/formats.md,
// "Arquivos de texto (*Txt.dat)".
//
// Txt_LoadLanguage carrega <idioma>Txt.dat do FILES.BIN (o nome vem de FNames[SelLang]: "UKTxt.Dat",
// "USATxt.Dat"...) para o heap: o bloco de strings (BankData), a tabela Bank (Bank[i] = string i; a 0
// é "") e a tabela de correção FixupTable. Um rótulo de texto (0x0200000d, por exemplo) é
// "tabela << 24 | índice": Txt_BindLabel devolve Bank[FixupTable[rótulo >> 24] + (rótulo & 0xffffff)].
// O índice 0 não é uma string do arquivo: devolve um buffer novo do StringHeap.
//
// StringHeap: 256 buffers de 0x168 bytes para textos montados (sprintf de "Picked up %s", quebra de
// linha das mensagens). Txt_GetStringFromHeap devolve o próximo buffer livre (trava = 0) a partir do
// último entregue, em rodízio; Txt_LockString/Txt_UnlockString contam quem ainda usa um buffer (strings
// fora do heap são ignoradas). Um buffer destravado volta a ser entregue 256 pedidos depois.
//
// O idioma: o main do PS2 põe LanguageToLoad = 6 (USA) no primeiro boot e o menu de idioma
// (P_LANGUAGE_Handler, sem o menu na versão americana) chama Txt_SetLanguage(LanguageToLoad). O port
// não tem o menu: nightfire.exe faz essa chamada no lugar dele. ResetMap_GameInit recarrega o arquivo a
// cada fase (Txt_LanguageInit), porque o Mem_Init da carga apaga o heap.
//
// Divergências (comentadas nas funções): Txt_BindLabel não lê além da FixupTable (tabela >= NumFixups
// devolve "Invalid Text Label" e conta em g_txt_guards); Txt_LoadLanguage não passa do fim do arquivo.
// Só do port: com o USATxt.dat, Txt_BindLabel devolve o texto do PC para os rótulos que falam do hardware do PS2
// (o cartão de memória, o controle DUALSHOCK®2, o multitap; a tabela em engine/pc_text.h, PcText_GameLabel).

#include <cstdint>

// tLANGUAGE: índice de FNames (0x002c7498).
enum : int32_t {
    TXTLANG_UK = 0,
    TXTLANG_FRENCH = 1,
    TXTLANG_GERMAN = 2,
    TXTLANG_SPANISH = 3,
    TXTLANG_ITALIAN = 4,
    TXTLANG_DUTCH = 5,
    TXTLANG_USA = 6,
    TXTLANG_JAPANESE = 7,
    TXTLANG_SWEDISH = 8,
};
constexpr int kLanguageCount = 9;

constexpr int kStringHeapCount = 0x100;
constexpr int kStringHeapSize = 0x168;

extern int32_t SelLang;          // 0x0030cee0 idioma carregado
extern int32_t LanguageToLoad;   // 0x0030d2ac idioma pedido (6 no primeiro boot, main do PS2)
extern char** Bank;              // 0x0030ced4 Bank[i]: string i (Bank[0] = "")
extern char* BankData;           // 0x0030ced8 as strings do arquivo
extern uint32_t NumEntries;      // 0x0030cedc
extern uint32_t NumFixups;       // 0x0030d7dc
extern uint32_t* FixupTable;     // 0x0030d7d8 primeiro índice de cada tabela de rótulos
extern char StringHeap[kStringHeapCount][kStringHeapSize];  // 0x00324d68
extern uint8_t StringHeapLock[kStringHeapCount];             // 0x0033b568
extern uint32_t StringHeapCnt;   // 0x0030ced0 próximo buffer a tentar
extern const char* const FNames[kLanguageCount];  // 0x002c7498 nome do arquivo de cada idioma

struct TxtGuards {
    uint32_t bad_table = 0;      // rótulo com tabela além da FixupTable (o original leria fora dela)
    uint32_t short_file = 0;     // arquivo menor do que o que ele declara
};
extern TxtGuards g_txt_guards;

void Txt_LanguageInit(int8_t cold);
void Txt_LoadLanguage();
void Txt_SetLanguage(int32_t language);
int32_t Txt_GetLanguage();
char* Txt_BindLabel(uint32_t label, int8_t lock);
char* Txt_GetStringFromHeap(int8_t lock);
void Txt_LockString(char* s);
void Txt_UnlockString(char* s);

// Só para testes: estado inicial (sem idioma carregado).
void txt_debug_power_on();
