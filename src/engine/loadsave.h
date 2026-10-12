#pragma once
// O perfil gravado no cartão (LS_*, 0x001cd0d8-0x001ce668 no ACTION.ELF): o arquivo de dados de um codinome é
// uma sequência de blocos "IFF" (uint32 rótulo, uint32 tamanho em bytes com o cabeçalho, bits empacotados por
// BIN_PushBits a partir do bit 0x40), um por entrada de SaveIFFBlocks (copiada do ELF na partida):
//
//   máscara  rótulo  conteúdo
//   1        PLRS    as opções do jogador (PlayerSetting: inverter, estilo, mira automática, agachar...)
//   2        MSSN    as missões liberadas (Menu_GetNightfireStatus), o recorde e os momentos Bond de cada
//                    missão (PlrStats_GetScoreTable) e GameState +0x52 (o jogo terminado)
//   4        MPSG    as opções do multijogador do jogador
//   8        GSET    volumes, idioma, legendas, som, widescreen, tela dividida, posição da tela
//   0x10     CHET    três trapaças (CheatInfo +0, +4, +8)
//   0x20     BNUS    as medalhas e bônus (Menu_GetBonus: 64 bits)
//
// LS_Save monta o arquivo em DstData[no] (os blocos fora da máscara vêm do último arquivo lido, InBufData[no]),
// pede a gravação ao cartão (psiSaveData, platform/psi_card.h, que acrescenta o crc) e guarda a cópia em
// InBufData; LS_Load lê o arquivo em InBufData[no] e entrega os blocos da máscara às funções de carga. As duas
// devolvem o estado de antes da chamada (0 parado, 1/2 andando, 7 deu certo, 8 falhou) e ficam em 7/8 até
// LS_FlushStates (PS2_DoCardFlow, no fim da operação).
//
// Divergências (só no port): a busca de um bloco para no tamanho zero (o original giraria para sempre num
// arquivo estragado) e LS_LoadMission lê no máximo 12 missões (ScoringTable; o original aceitaria até 255 e
// escreveria além das tabelas da pilha dele e de ScoringTable). LS_LoadPlrSettings e LS_LoadGlobalSettings, no fim,
// tomam as opções lidas como referência de platform/game_options.h (as de um codinome não vão para o nightfire.ini).

#include <cstdint>

// Uma entrada de SaveIFFBlocks (0x002c74d8, 16 bytes).
struct SAVEIFFBLOCK {
    uint32_t mask;                                              // 0x00 bit na máscara de LS_Save/LS_Load
    uint32_t id;                                                // 0x04 rótulo ("PLRS"...)
    uint8_t* (*make)(uint32_t id, uint16_t no, uint16_t* size);  // 0x08 monta o bloco
    void (*load)(uint8_t* block, uint16_t no);                  // 0x0c lê o bloco
};
static_assert(sizeof(SAVEIFFBLOCK) == 16);

constexpr int kSaveBlocks = 6;
constexpr uint32_t kLsSlotSize = 0x1000;  // um perfil por slot (no = jogador)
extern SAVEIFFBLOCK SaveIFFBlocks[kSaveBlocks];
extern uint32_t NumSaveBlocks;            // 0x0030cf10
extern uint8_t InBufData[4 * kLsSlotSize];  // 0x003416e0 o último arquivo lido (ou gravado) por slot
extern uint8_t DstData[4 * kLsSlotSize + 4];  // 0x003456e0 o arquivo montado (+4: o crc do último slot)
extern uint16_t Load_State;               // 0x0030cf14
extern uint16_t Save_State;               // 0x0030cf16

// [decomp]
void BIN_PushBits(uint8_t* buf, uint32_t* bit, uint32_t value, uint8_t count);
void BIN_PullBits(uint8_t* buf, uint32_t* bit, int32_t* out, uint8_t count);
void BIN_PullBits(uint8_t* buf, uint32_t* bit, uint8_t* out, uint8_t count);
void BIN_PullBits(uint8_t* buf, uint32_t* bit, uint16_t* out, uint8_t count);
void BIN_PullBits(uint8_t* buf, uint32_t* bit, uint32_t* out, uint8_t count);
void BIN_PullBits(uint8_t* buf, uint32_t* bit, int16_t* out, uint8_t count);
uint32_t LS_GetSaveStuff(uint16_t mask, uint16_t no);
void LS_GetLoadStuff(uint16_t mask, uint16_t no);
uint32_t LS_Save(char* name, uint16_t no, uint16_t mask);
uint32_t LS_Load(char* name, uint16_t no, uint16_t mask);
void LS_FlushStates();
bool LS_LoadFromBuffer(uint8_t* data, uint32_t size);
uint8_t* LS_MakePlrSettings(uint32_t id, uint16_t no, uint16_t* size);
void LS_LoadPlrSettings(uint8_t* block, uint16_t no);
uint8_t* LS_MakeMission(uint32_t id, uint16_t no, uint16_t* size);
void LS_LoadMission(uint8_t* block, uint16_t no);
uint8_t* LS_MakeGlobalSettings(uint32_t id, uint16_t no, uint16_t* size);
void LS_LoadGlobalSettings(uint8_t* block, uint16_t no);
uint8_t* LS_MakeCheats(uint32_t id, uint16_t no, uint16_t* size);
void LS_LoadCheats(uint8_t* block, uint16_t no);
uint8_t* LS_MakeBonus(uint32_t id, uint16_t no, uint16_t* size);
void LS_LoadBonus(uint8_t* block, uint16_t no);
uint8_t* LS_MakeMPSettings(uint32_t id, uint16_t no, uint16_t* size);
void LS_LoadMPSettings(uint8_t* block, uint16_t no);

// Os ponteiros de código de SaveIFFBlocks (game/static_init.cpp, engine/code_resolve.h).
uint32_t LS_ResolveCode(const char* symbol_name, uint32_t ps2_address);
// Só no port: o estado do módulo (testes; as tabelas copiadas do ELF ficam).
void loadsave_debug_power_on();
