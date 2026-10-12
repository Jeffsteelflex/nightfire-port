#pragma once
// Carregador de pacotes (LoaderLoad e companhia, 0x001d4880-0x001d6be4 no ACTION.ELF).
//
// Um pacote 07xxxxxx tem um setor de cabeçalho ({tamanho do diretório, número de arquivos}), o
// diretório (por arquivo: u32 tamanho, u8 tipo, nome terminado em zero) e os arquivos em sequência.
// LoaderLoad é uma máquina de estados chamada até devolver 0; cada chamada avança um passo e, no
// passo 4, entrega um arquivo por vez ao LoaderProcess, que despacha pelo tipo.

#include <cstdint>

// Tipo da carga. Muda a seção do hashcode (bits 20-23) do pacote pedido.
enum tLoaderType : uint32_t {
    LOADER_ANIMS = 0,      // seção 0xf: animações da fase (07Fxxxxx)
    LOADER_LEVEL = 1,      // o pacote como pedido; lê com Mem_SetMallocMethod(MALLOC_BOTTOM)
    LOADER_LOADABLE = 2,   // seção 7 + n (LoadableLoad)
    LOADER_RELOAD = 3,     // seção 0xf (LoadableReload)
    LOADER_COMMON = 4,     // CommonAnimFile
    LOADER_SECTION_D = 5,  // seção 0xd
};

// Arquivo atual do pacote (preenchidos no passo 4).
extern void* DirFileBuf;
extern uint32_t DirFileLen;
extern uint32_t DirFileType;
extern uint32_t DirFileHash;
extern char DirFileName[64];

extern char* LoadBufNext;   // fim do arquivo atual
extern char* LoadBufLast;   // fim do lote lido
extern void* LoaderAnimBase;
extern uint32_t CommonAnimFile;
extern uint8_t bLevelLoaded;
extern uint8_t PS2_Sound_Ready;
extern uint32_t MemType;    // 1 enquanto carrega um arquivo do tipo 0xb

struct LOADABLE_tag {
    uint32_t hash;
    uint8_t section;
    uint8_t pad[3];
};
extern LOADABLE_tag LoadableFiles[16];
extern uint8_t LoadableIndex;

uint32_t LoaderLoad(tLoaderType type, uint32_t file, uint8_t* buffer, uint32_t section);
uint32_t LoaderLoadAnims(tLoaderType type, uint32_t file, uint8_t* buffer, uint32_t section);
bool LoaderProcess();
void LoaderShrink(void** ptr, uint32_t size);
uint8_t isLoadable(uint32_t hash);
uint32_t LoadableLoad(uint32_t hash, uint32_t section);
uint32_t LoadableReload(uint32_t hash);

// Plataforma, mas usadas só pelo carregador.
bool MP_NeedFile(uint32_t hash);
int32_t CheckSize(int32_t* start, int32_t* end, uint32_t size);
int32_t CheckSizeAndShrink(int32_t* start, int32_t* end, uint32_t size);

// Só para o port: volta todo o estado estático do carregador ao valor inicial do executável.
void loader_debug_power_on();
