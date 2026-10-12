#pragma once
// Acesso a arquivos da plataforma (psiFile*, 0x0010ba38-0x0010bf2c no ACTION.ELF).
//
// No PS2 os arquivos do jogo estão dentro de FILES.BIN, indexado pela tabela FileList (118 entradas
// de 32 bytes no .data do executável). psiFileOpen procura o nome e posiciona o cursor; psiFileRead
// lê setores de 0x800 bytes a partir do cursor e o avança. No PC o cursor é um deslocamento em
// setores dentro de FILES.BIN, lido pela fonte do disco (platform/disc_source.h: uma pasta, a imagem ISO, o leitor de
// DVD ou a cópia pessoal).

#include <cstdint>

#include "platform/disc_source.h"

struct FILELIST_tag {
    char name[16];
    uint32_t offset;  // em bytes, dentro de FILES.BIN (múltiplo de 0x800)
    uint32_t size;
    uint32_t pad[2];
};
static_assert(sizeof(FILELIST_tag) == 32);

constexpr int kFileListCount = 0x76;
extern FILELIST_tag FileList[kFileListCount];

// Só no PC: abre FILES.BIN. FileList precisa estar preenchida antes (o port a lê do ACTION.ELF).
// Sem isso, m_file fica negativo e psiFileRead devolve nulo, como no PS2 sem disco.
bool psiFileSetup(nf::DiscFile files_bin);
// O FILES.BIN de um arquivo do PC (os testes).
bool psiFileSetup(const wchar_t* files_bin_path);
void psiFileShutdown();

int32_t psiFileOpen(const char* name);
void* psiFileRead(uint32_t size, uint32_t tag, uint32_t align, void* buffer);
void psiFileClose();
void* psiFileLoad(char* name, uint16_t tag, uint32_t* size);
int8_t psiFileSetSingleFileMode(int8_t single);

// Recebem arquivos carregados pelo LoaderProcess (tipos 15 e 16).
void SetUpIconFile(uint8_t* data, uint32_t size);
// O ícone do cartão de memória (o item tipo 15 do pacote do menu principal; MemCard_CREATE_DIR o grava).
extern uint8_t* PS2IconBuffer;  // 0x0030d6c8
extern uint32_t PS2IconSize;    // 0x0030d6c4 sempre 0x128f8
void PS2SetUpWoman(uint8_t* data);
