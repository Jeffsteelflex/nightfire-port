// Port do carregador de pacotes. Ver loader.h.

#include "engine/loader.h"

#include <cstdio>
#include <cstring>

#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/parsemap.h"
#include "engine/modules.h"
#include "engine/vecmath.h"
#include "platform/psi.h"
#include "platform/psi_file.h"
#include "platform/psi_woman.h"

void* DirFileBuf = nullptr;
uint32_t DirFileLen = 0;
uint32_t DirFileType = 0;
uint32_t DirFileHash = 0;
char DirFileName[64];
char* LoadBufNext = nullptr;
char* LoadBufLast = nullptr;
void* LoaderAnimBase = nullptr;
uint32_t CommonAnimFile = 0;
uint8_t bLevelLoaded = 0;
uint8_t PS2_Sound_Ready = 0;
uint32_t MemType = 0;
LOADABLE_tag LoadableFiles[16];
uint8_t LoadableIndex = 0;

namespace {

// Estáticas de LoaderLoad (os sufixos são os do símbolo original, ex.: LoadState.240).
uint32_t LoadState_240 = 0;
void* pDirHeader_241 = nullptr;  // setor de cabeçalho: [0] tamanho do diretório, [1] arquivos restantes
void* pDirData_242 = nullptr;
uint32_t MaxFilesToLoad_243 = 0xffffffff;
uint32_t FilesToLoad_244 = 0;
uint32_t BytesToLoad_245 = 0;
uint8_t* pDirDataCurFile_246 = nullptr;
void* LoadBufFirst_247 = nullptr;

// Modo de pacote já descomprimido na memória. Começa desligado e o jogo de varejo não o liga.
uint8_t Europacked = 0;
uint8_t* Euro_Decomp_Buf = nullptr;

uint32_t* dir_header() { return static_cast<uint32_t*>(pDirHeader_241); }

// Lê a entrada do diretório em p para DirFileLen, DirFileType, DirFileName (em maiúsculas) e
// DirFileHash (até 8 dígitos hexadecimais do nome, depois da última barra). Devolve a próxima.
// O original repete este trecho nos passos 2 e 4, com o mesmo resultado.
uint8_t* read_dir_entry(uint8_t* p) {
    std::memcpy(&DirFileLen, p, 4);
    DirFileType = p[4];
    p += 5;
    int len = 0;
    for (;;) {
        char c = static_cast<char>(*p++);
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 0x20);
        if (c == '\0') break;
        // Divergência: o original não limita o tamanho do nome (DirFileName tem 64 bytes).
        if (len < int(sizeof(DirFileName)) - 1) DirFileName[len++] = c;
    }
    DirFileName[len] = '\0';
    int start = len;
    while (start > 0 && DirFileName[start - 1] != '\\' && DirFileName[start - 1] != '/') --start;
    DirFileHash = 0;
    for (int digits = 0; digits < 8; ++digits) {
        const uint8_t ch = static_cast<uint8_t>(DirFileName[start + digits]);
        uint32_t d = uint32_t(ch) - 0x30;
        if (d > 9) d = uint32_t(ch) - 0x37;
        if (d > 0xf) break;
        DirFileHash = DirFileHash << 4 | d;
    }
    return p;
}

// Tabela de MP_NeedFile (0x0026d48c): itens de mapa que o multijogador dispensa e a skin de cada um.
struct MPFILE_tag {
    uint32_t hash;
    uint32_t unknown_04;
    uint8_t need;
    uint32_t skin;
};
const MPFILE_tag MPFiles[0x1d] = {
    {0x010001b4, 4, 0, 0x050000a5}, {0x010001ec, 4, 0, 0x0500008e}, {0x010001bc, 5, 0, 0x05000092},
    {0x010001c5, 6, 0, 0x05000095}, {0x010001c9, 6, 0, 0x05000096}, {0x010001d5, 6, 0, 0x05000097},
    {0x010001dd, 4, 0, 0x05000098}, {0x010001de, 4, 0, 0x0500009a}, {0x010001e1, 4, 0, 0x050000c6},
    {0x0100021b, 4, 0, 0x0500009e}, {0x010001e5, 5, 0, 0x050000a0}, {0x010001e7, 4, 0, 0x050000a4},
    {0x010001eb, 4, 0, 0x0500008b}, {0x010001b7, 4, 0, 0x050000bb}, {0x0100020b, 4, 0, 0x0500001c},
    {0x0100012f, 4, 0, 0x0500001d}, {0x0100012a, 4, 0, 0x0500001f}, {0x0100012d, 4, 0, 0x05000056},
    {0x0100015a, 6, 0, 0x05000060}, {0x0100016a, 6, 0, 0x05000063}, {0x01000170, 6, 0, 0x05000086},
    {0x010001af, 6, 0, 0x05000081}, {0x010001aa, 7, 0, 0x05000082}, {0x010001ab, 6, 0, 0x05000084},
    {0x010001ad, 4, 0, 0x0500006b}, {0x0100017e, 5, 0, 0x05000085}, {0x010001ae, 4, 0, 0x0500007b},
    {0x010001a1, 4, 0, 0x050000b9}, {0x01000208, 4, 0, 0x00000000},
};

}  // namespace

void loader_debug_power_on() {
    LoadState_240 = 0;
    pDirHeader_241 = pDirData_242 = nullptr;
    MaxFilesToLoad_243 = 0xffffffff;
    FilesToLoad_244 = BytesToLoad_245 = 0;
    pDirDataCurFile_246 = nullptr;
    LoadBufFirst_247 = nullptr;
    DirFileBuf = nullptr;
    DirFileLen = DirFileType = DirFileHash = 0;
    LoadBufNext = LoadBufLast = nullptr;
    LoaderAnimBase = nullptr;
    bLevelLoaded = 0;
    MemType = 0;
    LoadableIndex = 0;
    std::memset(LoadableFiles, 0, sizeof(LoadableFiles));
    psiFileSetSingleFileMode(0);
    parsemap_debug_power_on();
}

// [decomp] 0x001d4a00 LoaderLoad
uint32_t LoaderLoad(tLoaderType type, uint32_t file, uint8_t* buffer, uint32_t section) {
    uint8_t* entry = pDirDataCurFile_246;
    uint32_t result = 1;
    bLevelLoaded = 0;
    switch (LoadState_240) {
        case 0: {
            file &= 0xff0fffff;
            if (section != 0) file |= (section + 7) * 0x100000;
            if (type == LOADER_ANIMS || type == LOADER_RELOAD) file |= 0xf00000;
            if (type == LOADER_COMMON) file = CommonAnimFile;
            if (type == LOADER_SECTION_D) file |= 0xd00000;
            char name[256];
            std::snprintf(name, sizeof(name), "%s%8.8x.bin", "", file);
            psiFileSetSingleFileMode(1);
            LoadBufFirst_247 = nullptr;
            if (psiFileOpen(name) == 0) {
                result = 0;
            } else {
                if (type == LOADER_ANIMS || type == LOADER_COMMON || type == LOADER_SECTION_D) LoaderAnimBase = nullptr;
                pDirHeader_241 = nullptr;
                LoadState_240 = 1;
                pDirData_242 = nullptr;
            }
            break;
        }
        case 1:
            if (Europacked == 0) {
                pDirHeader_241 = psiFileRead(0x800, 0x4004, 0x80, nullptr);
            } else {
                pDirHeader_241 = Euro_Decomp_Buf;
                Euro_Decomp_Buf += 0x800;
            }
            if (dir_header()[1] != 0) {
                if (Europacked == 0) {
                    pDirData_242 = psiFileRead(dir_header()[0], 0x4004, 0x80, nullptr);
                } else {
                    pDirData_242 = Euro_Decomp_Buf;
                    Euro_Decomp_Buf += dir_header()[0];
                }
            }
            LoadState_240 = 2;
            pDirDataCurFile_246 = static_cast<uint8_t*>(pDirData_242);
            break;
        case 2: {
            const uint32_t remaining = dir_header()[1];
            FilesToLoad_244 = remaining < MaxFilesToLoad_243 ? remaining : MaxFilesToLoad_243;
            dir_header()[1] = remaining - FilesToLoad_244;
            BytesToLoad_245 = 0;
            for (uint32_t i = 0; i < FilesToLoad_244; ++i) {
                entry = read_dir_entry(entry);
                BytesToLoad_245 += DirFileLen;
            }
            LoadState_240 = 3;
            break;
        }
        case 3:
            if (FilesToLoad_244 != 0) {
                if (Europacked == 0) {
                    if (type == LOADER_LEVEL) {
                        const tMallocMethod old = Mem_SetMallocMethod(MALLOC_BOTTOM);
                        DirFileBuf = psiFileRead(BytesToLoad_245, 0x4104, 0x80, buffer);
                        if (PS2_Sound_Ready != 0) {
                            Sound_Ready();
                            PS2_Sound_Ready = 0;
                        }
                        Mem_SetMallocMethod(old);
                    } else {
                        DirFileBuf = psiFileRead(BytesToLoad_245, 0x4104, 0x80, buffer);
                    }
                    if (type == LOADER_ANIMS || type == LOADER_COMMON || type == LOADER_SECTION_D) LoadBufFirst_247 = DirFileBuf;
                } else {
                    DirFileBuf = Euro_Decomp_Buf;
                    Euro_Decomp_Buf += BytesToLoad_245;
                }
                LoadBufLast = static_cast<char*>(DirFileBuf) + BytesToLoad_245;
                if (LoaderAnimBase == nullptr) LoaderAnimBase = DirFileBuf;
            }
            LoadState_240 = 4;
            break;
        case 4:
            if (FilesToLoad_244 == 0) {
                LoadState_240 = dir_header()[1] == 0 ? 5 : 2;
            } else {
                entry = read_dir_entry(pDirDataCurFile_246);
                LoadBufNext = static_cast<char*>(DirFileBuf) + DirFileLen;
                if (LoaderProcess()) {
                    pDirDataCurFile_246 = entry;
                    --FilesToLoad_244;
                    DirFileBuf = static_cast<char*>(DirFileBuf) + DirFileLen;
                }
            }
            break;
        case 5:
            psiFileClose();
            psiFileSetSingleFileMode(0);
            if (Europacked == 0) {
                if (pDirData_242 != nullptr) Mem_Free(&pDirData_242);
                if (pDirHeader_241 != nullptr) Mem_Free(&pDirHeader_241);
            }
            LoadState_240 = 0;
            result = 0;
            break;
        default:
            break;
    }
    return result;
}

// [decomp] 0x001d4880 LoaderProcess
// Devolve true quando o arquivo atual terminou (parsemap_parsemap leva quatro chamadas).
bool LoaderProcess() {
    bool done = true;
    MemType = 0;
    uint8_t* data = static_cast<uint8_t*>(DirFileBuf);
    switch (DirFileType) {
        case 1:
            AnimPostLoadInit();
            done = parsemap_parsemap(DirFileHash, 1) == 0;
            break;
        case 3:
        case 4:
        case 5:
            AnimLoadFile(DirFileHash, 0);
            break;
        case 6:
            AnimSkeletonProcess(DirFileBuf);
            break;
        case 7:
            Script_Load(DirFileHash, const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR),
                        const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR), data, nullptr, nullptr, nullptr);
            break;
        case 8:
            MenuManager_Load(DirFileHash, data);
            break;
        case 0xb:
            MemType = 1;
            [[fallthrough]];
        case 0:
        case 2:
            done = parsemap_parsemap(DirFileHash, 0) == 0;
            break;
        case 0xc: {
            const uint8_t section = data[4];
            if (LoadableIndex < 16) {
                std::memcpy(&LoadableFiles[LoadableIndex].hash, data, 4);
                LoadableFiles[LoadableIndex].section = section;
                ++LoadableIndex;
            }
            break;
        }
        case 0xf:
            SetUpIconFile(data, DirFileLen);
            break;
        case 0x10:
            PS2SetUpWoman(data);
            psi_woman_set_end(data + DirFileLen);  // só do port: o fim do item, para as guardas
            break;
        default:
            break;
    }
    return done;
}

// [decomp] 0x001d6b48 LoaderLoadAnims
uint32_t LoaderLoadAnims(tLoaderType type, uint32_t file, uint8_t* buffer, uint32_t section) {
    return LoaderLoad(type, file, buffer, section);
}

// [decomp] 0x001d6b68 LoaderShrink
// Descarta o fim do arquivo atual a partir de *ptr: os arquivos seguintes do lote descem por cima
// dele e o bloco do lote encolhe. "size" não é usado. Zera *ptr.
void LoaderShrink(void** ptr, uint32_t) {
    char* discard = static_cast<char*>(*ptr);
    *ptr = nullptr;
    amemcpy(discard, LoadBufNext, size_t(LoadBufLast - LoadBufNext));
    const int32_t delta = int32_t(LoadBufNext - discard);
    LoadBufLast -= delta;
    DirFileLen -= delta;
    void* last = LoadBufLast;
    Mem_ShrinkToEndOfBlock(&last);
}

// [decomp] 0x001d69a0 isLoadable
uint8_t isLoadable(uint32_t hash) {
    for (uint32_t i = 0; i < LoadableIndex; ++i) {
        if (LoadableFiles[i].hash == hash) return LoadableFiles[i].section;
    }
    return 0;
}

// [decomp] 0x001d69e8 LoadableLoad
uint32_t LoadableLoad(uint32_t, uint32_t section) {
    AnimCacheDisable();
    const tMallocMethod old = Mem_SetMallocMethod(MALLOC_BEST);
    hashtable_delete_hashtype(0x4000000);
    const uint32_t saved = GameState.load_file;
    GameState.load_file = (GameState.level_file & 0xff0fffff) | (section + 7) * 0x100000;
    while (LoaderLoad(LOADER_LOADABLE, GameState.load_file, static_cast<uint8_t*>(LoaderAnimBase), section) != 0) {
    }
    GameState.load_file = saved;
    hashtable_create_sections();
    Mem_SetMallocMethod(old);
    return 1;
}

// [decomp] 0x001d6ab0 LoadableReload
uint32_t LoadableReload(uint32_t hash) {
    const tMallocMethod old = Mem_SetMallocMethod(MALLOC_BEST);
    Script_FreeFromHashTable(hash);
    AnimCacheEnable();
    hashtable_delete_hashtype(0x4000000);
    while (LoaderLoadAnims(LOADER_RELOAD, GameState.level_file, static_cast<uint8_t*>(LoaderAnimBase), 0) != 0) {
    }
    hashtable_create_sections();
    AnimPreLoadInit(1);
    Mem_SetMallocMethod(old);
    return 1;
}

// [decomp] 0x00120ca0 MP_NeedFile
bool MP_NeedFile(uint32_t hash) {
    if (GameState.multiplayer == 0) return true;
    for (const MPFILE_tag& f : MPFiles) {
        if (f.hash == hash) return f.need != 0;
    }
    return true;
}

// [decomp] 0x00120970 CheckSize
int32_t CheckSize(int32_t* start, int32_t* end, uint32_t) {
    return int32_t(reinterpret_cast<char*>(end) - reinterpret_cast<char*>(start));
}

// [decomp] 0x00120928 CheckSizeAndShrink
// Devolve os bytes usados; se o buffer tiver estourado ("size" menor que o usado), devolve o
// próprio ponteiro de fim, como o original.
int32_t CheckSizeAndShrink(int32_t* start, int32_t* end, uint32_t size) {
    const int32_t used = int32_t(reinterpret_cast<char*>(end) - reinterpret_cast<char*>(start));
    const int32_t spare = int32_t(size) - used;
    if (spare < 0) return int32_t(reinterpret_cast<intptr_t>(end));
    if (spare > 0) {
        void* p = end;
        Mem_Shrink(&p, uint32_t(spare));
    }
    return used;
}
