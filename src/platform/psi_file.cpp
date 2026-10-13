// Implementação para PC do acesso a arquivos psiFile*. Ver psi_file.h.

#include "platform/psi_file.h"

#include <cstdio>
#include <cstring>
#include <utility>

#include "engine/memory.h"
#include "platform/crt_compat.h"

// Globais do carregador (engine/loader.cpp) que psiFileLoad devolve no modo de arquivo único.
extern void* DirFileBuf;
extern uint32_t DirFileLen;

FILELIST_tag FileList[kFileListCount];

namespace {
nf::DiscFile g_files_bin;
int32_t m_file = -1;
int8_t bSingleFileMode = 0;
int32_t FileIndex = 0;
uint32_t filesize = 0;
uint32_t offset = 0;  // setor atual dentro de FILES.BIN (no PS2, somado ao LSN de FILES.BIN)

// O original compara com strupr nos dois lados (alterando as strings); aqui a comparação ignora
// maiúsculas sem alterar nada.
bool find_file(const char* name) {
    for (int i = 0; i < kFileListCount; ++i) {
        if (nf::str_nicmp(FileList[i].name, name, sizeof(FileList[i].name)) == 0 &&
            std::strlen(name) < sizeof(FileList[i].name)) {
            FileIndex = i;
            return true;
        }
    }
    return false;
}

// sceCdRead de "sectors" setores a partir do cursor.
void read_sectors(void* buffer, uint32_t sectors) {
    const size_t got = g_files_bin.read_at(uint64_t(offset) * 0x800, buffer, size_t(sectors) * 0x800);
    // O último arquivo pode terminar antes do fim do setor: o resto fica zerado.
    if (got < size_t(sectors) * 0x800) std::memset(static_cast<uint8_t*>(buffer) + got, 0, size_t(sectors) * 0x800 - got);
    offset += sectors;
}
}  // namespace

bool psiFileSetup(nf::DiscFile files_bin) {
    psiFileShutdown();
    g_files_bin = std::move(files_bin);
    m_file = g_files_bin.is_open() ? 0 : -1;
    return g_files_bin.is_open();
}

bool psiFileSetup(const wchar_t* files_bin_path) { return psiFileSetup(nf::DiscFile::open_host(files_bin_path)); }

void psiFileShutdown() {
    g_files_bin.close();
    m_file = -1;
}

// [decomp] 0x0010bd08 psiFileOpen
// Devolve 1 mesmo quando o nome não existe; nesse caso o cursor continua onde estava.
int32_t psiFileOpen(const char* name) {
    if (find_file(name)) {
        filesize = FileList[FileIndex].size;
        offset = FileList[FileIndex].offset >> 11;
    }
    return 1;
}

// [decomp] 0x0010bdf0 psiFileRead
// "tag" e "align" não são usados: o buffer novo é sempre alocado com a etiqueta 0x2980, que
// arredonda para setores. Com um buffer do chamador, a leitura também é arredondada para setores.
void* psiFileRead(uint32_t size, uint32_t, uint32_t, void* buffer) {
    if (m_file < 0) return nullptr;
    if (buffer == nullptr) buffer = Mem_Malloc(size, 0x2980, 0);
    if (buffer != nullptr) read_sectors(buffer, (size + 0x7ff) >> 11);
    return buffer;
}

// [decomp] 0x0010bde0 psiFileClose
void psiFileClose() {}

// [decomp] 0x0010ba38 psiFileLoad
// No modo de arquivo único (durante LoaderLoad) não lê nada: devolve o arquivo atual do pacote.
void* psiFileLoad(char* name, uint16_t, uint32_t* size) {
    if (bSingleFileMode != 0) {
        if (size != nullptr) *size = DirFileLen;
        return DirFileBuf;
    }
    if (find_file(name)) {
        filesize = FileList[FileIndex].size;
        offset = FileList[FileIndex].offset >> 11;
    }
    const uint32_t file_size = filesize;
    const uint32_t rounded = (filesize + 0x7ff) & 0xfffff800u;
    void* buffer = nullptr;
    if (m_file >= 0) {
        buffer = Mem_Malloc(rounded, 0x2980, 0);
        if (buffer != nullptr) read_sectors(buffer, rounded >> 11);
    }
    if (size != nullptr) *size = file_size;
    return buffer;
}

// [decomp] 0x0010bf20 psiFileSetSingleFileMode
int8_t psiFileSetSingleFileMode(int8_t single) {
    const int8_t old = bSingleFileMode;
    bSingleFileMode = single;
    return old;
}

// ---- arquivos especiais do pacote ----

uint8_t* PS2IconBuffer = nullptr;
uint32_t PS2IconSize = 0;

// [decomp] 0x00111270 SetUpIconFile
// O ícone do cartão de memória tem tamanho fixo; o tamanho do arquivo é ignorado.
void SetUpIconFile(uint8_t* data, uint32_t) {
    PS2IconBuffer = data;
    PS2IconSize = 0x128f8;
}
