// O perfil gravado no cartão: LS_* e BIN_PushBits/PullBits. Ver loadsave.h.

#include "engine/loadsave.h"

#include <cstring>

#include "engine/gamestate.h"
#include "engine/game.h"
#include "engine/input.h"
#include "engine/menu.h"
#include "engine/menu_frontend.h"
#include "engine/modules.h"
#include "engine/player_stats.h"
#include "engine/resetmap.h"
#include "engine/txt.h"
#include "platform/game_options.h"
#include "platform/psi.h"
#include "platform/psi_card.h"

SAVEIFFBLOCK SaveIFFBlocks[kSaveBlocks];
uint32_t NumSaveBlocks = 0;
uint8_t InBufData[4 * kLsSlotSize];
uint8_t DstData[4 * kLsSlotSize + 4];
uint16_t Load_State = 0;
uint16_t Save_State = 0;

namespace {

// LS_Save
uint32_t OldState_66 = 0;
uint16_t WW_62 = 0;
uint16_t No_63 = 0;
uint32_t Size_64 = 0;
int32_t ret_65 = 0;
// LS_Load
uint32_t OldState_58 = 0;
uint32_t Size_56 = 0;
uint16_t WW_54 = 0;
uint16_t No_55 = 0;
int32_t ret_57 = 0;
// Os blocos montados (0x1000 bytes cada, 0x0033b6e0 em diante).
uint8_t Buf_88[0x1000];   // PLRS
uint8_t Buf_95[0x1000];   // CHET
uint8_t Buf_102[0x1000];  // MSSN
uint8_t Buf_109[0x1000];  // BNUS
uint8_t Buf_116[0x1000];  // MPSG
uint8_t Buf_123[0x1000];  // GSET

uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
void wr32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

// O cabeçalho de um bloco: o rótulo no começo, o tamanho em bytes (bits arredondados) em +4.
void begin(uint8_t* buf, uint32_t id, uint32_t* bit) {
    *bit = 0x40;
    wr32(buf, id);
}
uint8_t* finish(uint8_t* buf, uint32_t bit, uint16_t* size) {
    const uint32_t bytes = (bit + 7) >> 3;
    wr32(buf + 4, bytes);
    *size = uint16_t(bytes);
    return buf;
}

// O bloco `id` no arquivo do slot (a lista termina num rótulo 0 ou no fim do slot); nulo sem ele.
uint8_t* find_block(uint16_t no, uint32_t id) {
    uint8_t* p = InBufData + uint32_t(no) * kLsSlotSize;
    uint32_t pos = 0;
    for (;;) {
        if (rd32(p) == id) return p;
        if (rd32(p) == 0) return nullptr;
        const uint32_t size = rd32(p + 4);
        if (size == 0) return nullptr;  // só no port: o original giraria para sempre
        p += size;
        pos += size;
        if (0xfff < pos) return nullptr;
    }
}

}  // namespace

// [decomp] 0x001e8b88 BIN_PushBits
// Escreve os `count` bits baixos de value a partir do bit *bit de buf (bit 0 primeiro) e avança *bit.
void BIN_PushBits(uint8_t* buf, uint32_t* bit, uint32_t value, uint8_t count) {
    const uint32_t start = *bit;
    uint32_t b = start & 7;
    uint8_t* p = buf + (start >> 3);
    for (uint32_t i = 0; i < count; i = (i + 1) & 0xffff) {
        *p = uint8_t((*p & ~(1u << b)) | (((value >> (i & 0x1f)) & 1) << b));
        if (7 < ++b) {
            ++p;
            b = 0;
        }
    }
    *bit = start + count;
}

namespace {
uint32_t pull(const uint8_t* buf, uint32_t* bit, uint8_t count) {
    const uint32_t start = *bit;
    uint32_t b = start & 7;
    uint32_t byte = start >> 3;
    uint32_t v = 0;
    for (uint32_t i = 0; i < count; i = (i + 1) & 0xffff) {
        v |= ((uint32_t(buf[byte]) >> b) & 1) << (i & 0x1f);
        if (7 < ++b) {
            ++byte;
            b = 0;
        }
    }
    *bit = start + count;
    return v;
}
}  // namespace

// [decomp] 0x001e8c18 BIN_PullBits
void BIN_PullBits(uint8_t* buf, uint32_t* bit, int32_t* out, uint8_t count) { *out = int32_t(pull(buf, bit, count)); }

// [decomp] 0x001e8ca0 BIN_PullBits
void BIN_PullBits(uint8_t* buf, uint32_t* bit, uint8_t* out, uint8_t count) { *out = uint8_t(pull(buf, bit, count)); }

// [decomp] 0x001e8d28 BIN_PullBits
void BIN_PullBits(uint8_t* buf, uint32_t* bit, uint16_t* out, uint8_t count) { *out = uint16_t(pull(buf, bit, count)); }

// [decomp] 0x001e8db0 BIN_PullBits
void BIN_PullBits(uint8_t* buf, uint32_t* bit, uint32_t* out, uint8_t count) { *out = pull(buf, bit, count); }

// [decomp] 0x001e8e38 BIN_PullBits
void BIN_PullBits(uint8_t* buf, uint32_t* bit, int16_t* out, uint8_t count) { *out = int16_t(pull(buf, bit, count)); }

// [decomp] 0x001cd0d8 LS_GetSaveStuff
// Monta o arquivo do slot em DstData: os blocos da máscara pelas funções de montagem, os outros copiados do
// último arquivo lido (InBufData). Devolve o tamanho.
uint32_t LS_GetSaveStuff(uint16_t mask, uint16_t no) {
    uint32_t offset = 0;
    for (uint32_t i = 0; i < NumSaveBlocks && i < uint32_t(kSaveBlocks); i = (i + 1) & 0xffff) {
        const SAVEIFFBLOCK& block = SaveIFFBlocks[i];
        uint16_t size = 0;
        uint8_t* src = nullptr;
        if ((mask & block.mask) == 0) {
            src = find_block(no, block.id);
            if (src != nullptr) std::memcpy(&size, src + 4, 2);
        } else if (block.make != nullptr) {
            src = block.make(block.id, no, &size);
        }
        if (src == nullptr) continue;
        if (kLsSlotSize < offset + size) break;  // só no port: dentro do slot
        amemcpy(DstData + uint32_t(no) * kLsSlotSize + offset, src, size);
        offset = (offset + size) & 0xffff;
    }
    return offset;
}

// [decomp] 0x001cd2b0 LS_GetLoadStuff
// Entrega os blocos da máscara do arquivo lido (InBufData do slot) às funções de carga.
void LS_GetLoadStuff(uint16_t mask, uint16_t no) {
    for (uint32_t i = 0; i < NumSaveBlocks && i < uint32_t(kSaveBlocks); i = (i + 1) & 0xffff) {
        const SAVEIFFBLOCK& block = SaveIFFBlocks[i];
        if ((mask & block.mask) == 0) continue;
        uint8_t* p = find_block(no, block.id);
        if (p != nullptr && block.load != nullptr) block.load(p, no);
    }
}

// [decomp] 0x001ce0c0 LS_Save
// Grava o perfil `name` com os blocos da máscara; devolve o estado de antes (0, 2 gravando, 7 pronto, 8 falhou).
uint32_t LS_Save(char* name, uint16_t no, uint16_t mask) {
    OldState_66 = Save_State;
    if (Save_State != 2) {
        if (2 < Save_State || Save_State != 0) return OldState_66;
        WW_62 = mask;
        No_63 = no;
        Size_64 = LS_GetSaveStuff(mask, no);
        psiSaveData(DstData + uint32_t(No_63) * kLsSlotSize, Size_64, name);
        amemcpy(InBufData + uint32_t(No_63) * kLsSlotSize, DstData + uint32_t(No_63) * kLsSlotSize, Size_64);
        Save_State = 2;
    }
    const uint32_t r = psiSavingData();
    if (r == 2) {
        Save_State = 7;
    } else if (r == 4) {
        Save_State = 8;
    }
    ret_65 = int32_t(r);
    return OldState_66;
}

// [decomp] 0x001cdfc0 LS_Load
// Lê o perfil `name` e entrega os blocos da máscara; devolve o estado de antes (0, 1 lendo, 7 pronto, 8 falhou).
uint32_t LS_Load(char* name, uint16_t no, uint16_t mask) {
    OldState_58 = Load_State;
    if (OldState_58 != 1) {
        if (1 < Load_State || OldState_58 != 0) return OldState_58;
        Size_56 = kLsSlotSize;
        WW_54 = mask;
        No_55 = no;
        std::memset(InBufData + uint32_t(no) * kLsSlotSize, 0, kLsSlotSize);
        psiLoadData(InBufData + uint32_t(No_55) * kLsSlotSize, &Size_56, name);
        Load_State = 1;
    }
    const uint32_t r = psiLoadingData();
    ret_57 = int32_t(r);
    if (r == 2) {
        Load_State = 7;
        LS_GetLoadStuff(mask, No_55);
    } else if (r == 4) {
        Load_State = 8;
    }
    return OldState_58;
}

// [decomp] 0x001ce1b0 LS_FlushStates
void LS_FlushStates() {
    Load_State = 0;
    Save_State = 0;
}

// [decomp] 0x001ce1c8 LS_LoadFromBuffer
// Um arquivo vindo de fora (os dados que o DRIVING.ELF devolve) vira o último lido do slot 0.
bool LS_LoadFromBuffer(uint8_t* data, uint32_t size) {
    const bool any = (data[0] | data[1] | data[2] | data[3]) != 0;
    if (any) {
        std::memset(InBufData, 0, kLsSlotSize);
        amemcpy(InBufData, data, size < kLsSlotSize ? size : kLsSlotSize);  // só no port: dentro do slot
    }
    return any;
}

// [decomp] 0x001cd418 LS_MakePlrSettings
uint8_t* LS_MakePlrSettings(uint32_t id, uint16_t no, uint16_t* size) {
    uint32_t bit;
    begin(Buf_88, id, &bit);
    const PlayerInput_tag& p = PlayerSetting[no];
    BIN_PushBits(Buf_88, &bit, p.invert_look, 1);
    BIN_PushBits(Buf_88, &bit, uint32_t(int32_t(int16_t(p.style))), 4);
    BIN_PushBits(Buf_88, &bit, uint32_t(int32_t(int16_t(p.unknown_10))), 4);
    BIN_PushBits(Buf_88, &bit, p.autoaim, 1);
    BIN_PushBits(Buf_88, &bit, p.autoaim_mp, 1);
    BIN_PushBits(Buf_88, &bit, p.aim_toggle, 1);
    BIN_PushBits(Buf_88, &bit, p.auto_switch, 1);
    BIN_PushBits(Buf_88, &bit, p.crouch_toggle, 1);
    BIN_PushBits(Buf_88, &bit, p.unknown_09, 1);
    BIN_PushBits(Buf_88, &bit, p.unknown_08, 1);
    BIN_PushBits(Buf_88, &bit, p.unknown_0c, 2);
    BIN_PushBits(Buf_88, &bit, p.unknown_0b, 1);
    return finish(Buf_88, bit, size);
}

// [decomp] 0x001cd5b8 LS_LoadPlrSettings
// Só no port, no fim: as opções lidas viram a referência de platform/game_options.h (o .ini não muda).
void LS_LoadPlrSettings(uint8_t* block, uint16_t no) {
    uint32_t bit = 0x40;
    PlayerInput_tag& p = PlayerSetting[no];
    BIN_PullBits(block, &bit, &p.invert_look, 1);
    BIN_PullBits(block, &bit, &p.style, 4);
    BIN_PullBits(block, &bit, &p.unknown_10, 4);
    BIN_PullBits(block, &bit, &p.autoaim, 1);
    BIN_PullBits(block, &bit, &p.autoaim_mp, 1);
    BIN_PullBits(block, &bit, &p.aim_toggle, 1);
    BIN_PullBits(block, &bit, &p.auto_switch, 1);
    BIN_PullBits(block, &bit, &p.crouch_toggle, 1);
    BIN_PullBits(block, &bit, &p.unknown_09, 1);
    BIN_PullBits(block, &bit, &p.unknown_08, 1);
    BIN_PullBits(block, &bit, &p.unknown_0c, 2);
    BIN_PullBits(block, &bit, &p.unknown_0b, 1);
    if (no == 0) nf::game_options_rebase();  // só no port: as opções do codinome não vão para o nightfire.ini
}

// [decomp] 0x001cd720 LS_MakeMission
// As missões liberadas, o recorde e os momentos Bond (a primeira linha da pontuação) de cada missão.
uint8_t* LS_MakeMission(uint32_t id, uint16_t, uint16_t* size) {
    uint32_t bit;
    begin(Buf_102, id, &bit);
    BIN_PushBits(Buf_102, &bit, Menu_GetNightfireStatus(), 32);
    uint32_t count = 0;
    const SCORETABLE* table = PlrStats_GetScoreTable(&count);
    BIN_PushBits(Buf_102, &bit, count, 8);
    for (uint32_t i = 0; i < count && table != nullptr; ++i) {
        BIN_PushBits(Buf_102, &bit, table[i].best, 32);
        // Só no port: sem linhas, 0 (o original leria por nulo).
        BIN_PushBits(Buf_102, &bit, table[i].rows != nullptr ? table[i].rows[0].actual : 0, 4);
    }
    BIN_PushBits(Buf_102, &bit, GameState.unknown_52, 1);
    return finish(Buf_102, bit, size);
}

// [decomp] 0x001cd880 LS_LoadMission
void LS_LoadMission(uint8_t* block, uint16_t) {
    uint32_t bit = 0x40;
    uint32_t status = 0;
    BIN_PullBits(block, &bit, &status, 32);
    Menu_SetNightfireStatus(status);
    SCORETABLE tables[20];
    SCORETABLE_ROW rows[20][kScoreRows];
    std::memset(tables, 0, sizeof(tables));
    std::memset(rows, 0, sizeof(rows));
    uint32_t count = 0;
    BIN_PullBits(block, &bit, &count, 8);
    // Só no port: no máximo as 12 missões de ScoringTable (um arquivo estragado escreveria além das tabelas).
    if (kScoringTableCount < count) count = kScoringTableCount;
    for (uint32_t i = 0; i < count; ++i) {
        tables[i].rows = rows[i];
        BIN_PullBits(block, &bit, &tables[i].best, 32);
        BIN_PullBits(block, &bit, &rows[i][0].actual, 4);
    }
    BIN_PullBits(block, &bit, &GameState.unknown_52, 1);
    PlrStats_SetupScoreTable(tables, count);
}

// [decomp] 0x001cd9d0 LS_MakeGlobalSettings
// Volumes, idioma, legendas (DrawInfo[2]), modo do som, widescreen (DrawInfo[1]), tela dividida
// (DrawInfo[0]), a posição da tela e 219 palavras reservadas; a versão 4 no fim.
uint8_t* LS_MakeGlobalSettings(uint32_t id, uint16_t, uint16_t* size) {
    uint32_t bit;
    begin(Buf_123, id, &bit);
    const uint32_t language = uint32_t(Txt_GetLanguage());
    const uint32_t music = SFXMusicGetVolume();
    const uint32_t sfx = SFXGetVolume();
    const uint32_t mode = SFXGetMode();
    BIN_PushBits(Buf_123, &bit, music, 7);
    BIN_PushBits(Buf_123, &bit, sfx, 7);
    BIN_PushBits(Buf_123, &bit, language, 7);
    BIN_PushBits(Buf_123, &bit, DrawInfo[2], 1);
    BIN_PushBits(Buf_123, &bit, mode, 32);
    BIN_PushBits(Buf_123, &bit, DrawInfo[1], 1);
    BIN_PushBits(Buf_123, &bit, DrawInfo[0], 32);
    int32_t x = 0, y = 0;
    psiGetScreenPos(&x, &y);
    BIN_PushBits(Buf_123, &bit, uint32_t(x), 32);
    BIN_PushBits(Buf_123, &bit, uint32_t(y), 32);
    for (int i = 0; i < 0xdb; ++i) BIN_PushBits(Buf_123, &bit, 0, 32);
    BIN_PushBits(Buf_123, &bit, 4, 16);
    return finish(Buf_123, bit, size);
}

// [decomp] 0x001cdbc8 LS_LoadGlobalSettings
// O idioma gravado é lido e ignorado. Só no port, no fim: as opções lidas viram a referência de
// platform/game_options.h (o .ini não muda).
void LS_LoadGlobalSettings(uint8_t* block, uint16_t) {
    uint32_t bit = 0x40;
    uint32_t music = 0, sfx = 0, language = 0, mode = 0, wide = 0;
    BIN_PullBits(block, &bit, &music, 7);
    BIN_PullBits(block, &bit, &sfx, 7);
    BIN_PullBits(block, &bit, &language, 7);
    BIN_PullBits(block, &bit, &DrawInfo[2], 1);
    BIN_PullBits(block, &bit, &mode, 32);
    BIN_PullBits(block, &bit, &wide, 1);
    BIN_PullBits(block, &bit, &DrawInfo[0], 32);
    int32_t x = 0, y = 0;
    BIN_PullBits(block, &bit, &x, 32);
    BIN_PullBits(block, &bit, &y, 32);
    psiSetScreenPos(x, y);
    int32_t reserved = 0;
    for (int i = 0; i < 0xdb; ++i) BIN_PullBits(block, &bit, &reserved, 32);
    uint32_t version = 0;
    BIN_PullBits(block, &bit, &version, 16);
    SFXSetMode(int32_t(mode));
    DrawInfo[1] = wide;
    SFXMusicSetVolume(music);
    SFXSetVolume(sfx);
    nf::game_options_rebase();  // só no port: as opções do codinome não vão para o nightfire.ini
}

// [decomp] 0x001ce260 LS_MakeCheats
uint8_t* LS_MakeCheats(uint32_t id, uint16_t, uint16_t* size) {
    uint32_t bit;
    begin(Buf_95, id, &bit);
    BIN_PushBits(Buf_95, &bit, rd32(CheatInfo), 1);
    BIN_PushBits(Buf_95, &bit, rd32(CheatInfo + 4), 1);
    BIN_PushBits(Buf_95, &bit, rd32(CheatInfo + 8), 1);
    return finish(Buf_95, bit, size);
}

// [decomp] 0x001ce340 LS_LoadCheats
void LS_LoadCheats(uint8_t* block, uint16_t) {
    uint32_t bit = 0x40;
    uint32_t v = 0;
    BIN_PullBits(block, &bit, &v, 1);
    wr32(CheatInfo, v);
    BIN_PullBits(block, &bit, &v, 1);
    wr32(CheatInfo + 4, v);
    BIN_PullBits(block, &bit, &v, 1);
    wr32(CheatInfo + 8, v);
}

// [decomp] 0x001ce3b0 LS_MakeBonus
// As medalhas e bônus do jogador 0 (64 bits, a metade alta primeiro).
uint8_t* LS_MakeBonus(uint32_t id, uint16_t, uint16_t* size) {
    uint32_t bit;
    begin(Buf_109, id, &bit);
    const uint64_t bonus = Menu_GetBonus(0);
    BIN_PushBits(Buf_109, &bit, uint32_t(bonus >> 32), 32);
    BIN_PushBits(Buf_109, &bit, uint32_t(bonus), 32);
    return finish(Buf_109, bit, size);
}

// [decomp] 0x001ce488 LS_LoadBonus
void LS_LoadBonus(uint8_t* block, uint16_t no) {
    uint32_t bit = 0x40;
    uint32_t hi = 0, lo = 0;
    BIN_PullBits(block, &bit, &hi, 32);
    BIN_PullBits(block, &bit, &lo, 32);
    Menu_SetBonus((uint64_t(hi) << 32) | lo, uint8_t(no), 0);
}

// [decomp] 0x001ce508 LS_MakeMPSettings
uint8_t* LS_MakeMPSettings(uint32_t id, uint16_t no, uint16_t* size) {
    uint32_t bit;
    begin(Buf_116, id, &bit);
    BIN_PushBits(Buf_116, &bit, rd32(MPSettings + uint32_t(no) * 0x30 + 0x28), 1);
    BIN_PushBits(Buf_116, &bit, rd32(MPSettings + uint32_t(no) * 0x30 + 0x2c), 32);
    return finish(Buf_116, bit, size);
}

// [decomp] 0x001ce5f0 LS_LoadMPSettings
void LS_LoadMPSettings(uint8_t* block, uint16_t no) {
    uint32_t bit = 0x40;
    uint32_t v = 0;
    BIN_PullBits(block, &bit, &v, 1);
    wr32(MPSettings + uint32_t(no) * 0x30 + 0x28, v);
    BIN_PullBits(block, &bit, &v, 32);
    wr32(MPSettings + uint32_t(no) * 0x30 + 0x2c, v);
}

uint32_t LS_ResolveCode(const char* symbol_name, uint32_t) {
    struct Entry {
        const char* name;
        uint32_t address;
    };
    const Entry table[] = {
        {"LS_MakePlrSettings__FUiUsPUs", nf::p32_fn_u(&LS_MakePlrSettings)},
        {"LS_LoadPlrSettings__FPUcUs", nf::p32_fn_u(&LS_LoadPlrSettings)},
        {"LS_MakeMission__FUiUsPUs", nf::p32_fn_u(&LS_MakeMission)},
        {"LS_LoadMission__FPUcUs", nf::p32_fn_u(&LS_LoadMission)},
        {"LS_MakeMPSettings__FUiUsPUs", nf::p32_fn_u(&LS_MakeMPSettings)},
        {"LS_LoadMPSettings__FPUcUs", nf::p32_fn_u(&LS_LoadMPSettings)},
        {"LS_MakeGlobalSettings__FUiUsPUs", nf::p32_fn_u(&LS_MakeGlobalSettings)},
        {"LS_LoadGlobalSettings__FPUcUs", nf::p32_fn_u(&LS_LoadGlobalSettings)},
        {"LS_MakeCheats__FUiUsPUs", nf::p32_fn_u(&LS_MakeCheats)},
        {"LS_LoadCheats__FPUcUs", nf::p32_fn_u(&LS_LoadCheats)},
        {"LS_MakeBonus__FUiUsPUs", nf::p32_fn_u(&LS_MakeBonus)},
        {"LS_LoadBonus__FPUcUs", nf::p32_fn_u(&LS_LoadBonus)},
    };
    for (const Entry& e : table) {
        if (std::strcmp(e.name, symbol_name) == 0) return e.address;
    }
    return 0;
}

void loadsave_debug_power_on() {
    std::memset(InBufData, 0, sizeof(InBufData));
    std::memset(DstData, 0, sizeof(DstData));
    Load_State = 0;
    Save_State = 0;
    OldState_66 = OldState_58 = 0;
    WW_62 = No_63 = WW_54 = No_55 = 0;
    Size_64 = Size_56 = 0;
    ret_65 = ret_57 = 0;
}
