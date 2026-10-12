// Os códigos de "Secret Unlocks" (a primeira opção do menu do codinome: o teclado da tela em modo de código,
// C_KEYBOARD_Handler): Menu_SpecialCodenameCheck e Menu_UpgradeCheat. Ver menu_frontend.h.
//
// Cada código libera missões (a máscara das 12 de sp_level) ou bits de `bonus` (os personagens, os cenários e as
// armas do multijogador e as melhorias de armas, engine/menu_dossier.h). Os códigos de missão e de bônus são um "ou
// exclusivo": digitar de novo desfaz. Os nomes dos códigos são as strings do .rodata do ACTION.ELF (0x00308578 em
// diante); a tabela abaixo segue a ordem do original, que a monta na pilha a cada chamada. nf_loader_check confere
// cada código com o código do ELF no interpretador MIPS (game/menu_tables.h, check_special_codenames).

#include "engine/menu_frontend.h"

#include <cstring>

#include "engine/menu.h"
#include "engine/mission.h"

namespace {

uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
M_ITEM* sp_items() { return reinterpret_cast<M_ITEM*>(sp_level); }

// Como cada código monta a sua entrada (+0x20 as missões, +0x28 os bônus) a partir da máscara das missões
// liberadas (m), de `bonus` (b) e das melhorias calculadas por Menu_UpgradeCheat.
enum class Missions : uint8_t { All, Toggle, Keep };
enum class Bonus : uint8_t { Keep, Xor, Or, Upgrade, Upgrades };
struct SecretCode {
    const char* code;
    Missions missions;
    uint32_t mission_bits;  // Toggle: m ^ bits
    Bonus bonus;
    uint64_t bonus_bits;    // Xor/Or: b ^ / | bits; Upgrade: o índice da chamada a Menu_UpgradeCheat
};

// As 12 chamadas a Menu_UpgradeCheat(bonus, bit, quantas), na ordem do original.
struct UpgradeCall {
    uint32_t bit;
    uint8_t count;
};
constexpr UpgradeCall kUpgrades[12] = {{0, 1}, {3, 1}, {6, 1}, {9, 1}, {0, 2}, {0xc, 1},
                                       {0, 3}, {0xf, 1}, {0x12, 1}, {0x15, 1}, {9, 2}, {0x18, 1}};

constexpr SecretCode kCodes[52] = {
    {"PASSPORT", Missions::All, 0, Bonus::Keep, 0},
    {"POWDER", Missions::Toggle, 0x4, Bonus::Keep, 0},
    {"TRACTION", Missions::Toggle, 0x8, Bonus::Keep, 0},
    {"BONSAI", Missions::Toggle, 0x10, Bonus::Keep, 0},
    {"HIGHRISE", Missions::Toggle, 0x20, Bonus::Keep, 0},
    {"MELTDOWN", Missions::Toggle, 0x40, Bonus::Keep, 0},
    {"FLAME", Missions::Toggle, 0x80, Bonus::Keep, 0},
    {"AQUA", Missions::Toggle, 0x100, Bonus::Keep, 0},
    {"PARADISE", Missions::Toggle, 0x200, Bonus::Keep, 0},
    {"BLASTOFF", Missions::Toggle, 0x400, Bonus::Keep, 0},
    {"VACUUM", Missions::Toggle, 0x800, Bonus::Keep, 0},
    {"PARTY", Missions::Keep, 0, Bonus::Or, 0x00ffefc000000000ull},
    {"BOWLER", Missions::Keep, 0, Bonus::Xor, 0x0000004000000000ull},
    {"BLACKTIE", Missions::Keep, 0, Bonus::Xor, 0x0000008000000000ull},
    {"MARTIAL", Missions::Keep, 0, Bonus::Xor, 0x0000010000000000ull},
    {"BITESIZE", Missions::Keep, 0, Bonus::Xor, 0x0000020000000000ull},
    {"JOELWADE", Missions::Keep, 0, Bonus::Xor, 0x0000020000000000ull},
    {"ASSASSIN", Missions::Keep, 0, Bonus::Xor, 0x0000040000000000ull},
    {"DENTAL", Missions::Keep, 0, Bonus::Xor, 0x0000080000000000ull},
    {"NUMBER 1", Missions::Keep, 0, Bonus::Xor, 0x0040000000000000ull},
    {"VOODOO", Missions::Keep, 0, Bonus::Xor, 0x0000200000000000ull},
    {"JANUS", Missions::Keep, 0, Bonus::Xor, 0x0000400000000000ull},
    {"MIDAS", Missions::Keep, 0, Bonus::Xor, 0x0000800000000000ull},
    {"BADGIRL", Missions::Keep, 0, Bonus::Xor, 0x0001000000000000ull},
    {"SLICK", Missions::Keep, 0, Bonus::Xor, 0x0002000000000000ull},
    {"NUCLEAR", Missions::Keep, 0, Bonus::Xor, 0x0004000000000000ull},
    {"CIRCUS", Missions::Keep, 0, Bonus::Xor, 0x0008000000000000ull},
    {"HEADCASE", Missions::Keep, 0, Bonus::Xor, 0x0010000000000000ull},
    {"BLIMP", Missions::Keep, 0, Bonus::Xor, 0x0020000000000000ull},
    {"HUGE EGO", Missions::Keep, 0, Bonus::Xor, 0x0020000000000000ull},
    {"ZERO G", Missions::Keep, 0, Bonus::Xor, 0x0080000000000000ull},
    {"GAMEROOM", Missions::Keep, 0, Bonus::Or, 0x3f00000000000000ull},
    {"TARGET", Missions::Keep, 0, Bonus::Xor, 0x0100000000000000ull},
    {"TRANSMIT", Missions::Keep, 0, Bonus::Xor, 0x0200000000000000ull},
    {"TEAMWORK", Missions::Keep, 0, Bonus::Xor, 0x0400000000000000ull},
    {"TNT", Missions::Keep, 0, Bonus::Xor, 0x0800000000000000ull},
    {"GUARDIAN", Missions::Keep, 0, Bonus::Xor, 0x1000000000000000ull},
    {"ORBIT", Missions::Keep, 0, Bonus::Xor, 0x2000000000000000ull},
    {"BOOM", Missions::Keep, 0, Bonus::Xor, 0x4000000000000000ull},
    {"Q LAB", Missions::Keep, 0, Bonus::Upgrades, 0},
    {"AU PP7", Missions::Keep, 0, Bonus::Upgrade, 0},
    {"LIFTOFF", Missions::Keep, 0, Bonus::Upgrade, 1},
    {"SHUTTER", Missions::Keep, 0, Bonus::Upgrade, 2},
    {"SCOPE", Missions::Keep, 0, Bonus::Upgrade, 3},
    {"P2000", Missions::Keep, 0, Bonus::Upgrade, 4},
    {"SLEEPY", Missions::Keep, 0, Bonus::Upgrade, 5},
    {"AU P2K", Missions::Keep, 0, Bonus::Upgrade, 6},
    {"SESAME", Missions::Keep, 0, Bonus::Upgrade, 7},
    {"ZAP", Missions::Keep, 0, Bonus::Upgrade, 8},
    {"PHOTON", Missions::Keep, 0, Bonus::Upgrade, 9},
    {"MAGAZINE", Missions::Keep, 0, Bonus::Upgrade, 10},
    {"LAUNCH", Missions::Keep, 0, Bonus::Upgrade, 11},
};

}  // namespace

// [decomp] 0x001fffe8 Menu_UpgradeCheat
// Liga em `bits` até `count` medalhas de missão cuja recompensa é a melhoria `bit` (tipo 4 de
// PlrStarts_ProcessRewardCounter): primeiro conta as já ligadas; se faltam, liga as seguintes, missão por missão e
// medalha por medalha (1 a 4), até chegar a `count`. Devolve os bits.
uint64_t Menu_UpgradeCheat(uint64_t bits, uint32_t bit, uint8_t count) {
    REWARDINFO_tag info{};
    uint32_t found = 0;
    for (uint32_t i = 0; i < 12; ++i) {
        const uint32_t level = rd32(sp_level + i * 0x18 + 0xc);
        for (uint32_t medal = 1; medal < 5; ++medal) {
            PlrStarts_ProcessRewardCounter(&bits, level, medal, 0, &info);
            if (info.has != 0 && info.kind == 4 && info.bit == bit) found = (found + 1) & 0xff;
        }
    }
    if (count <= found) return bits;
    for (uint32_t i = 0; i < 12; ++i) {
        const uint32_t level = rd32(sp_level + i * 0x18 + 0xc);
        for (uint32_t medal = 1; medal < 5; ++medal) {
            PlrStarts_ProcessRewardCounter(&bits, level, medal, 0, &info);
            if (info.kind != 4) continue;
            if (info.has == 0 && info.bit == bit) {
                found = (found + 1) & 0xff;
                PlrStarts_ProcessRewardCounter(&bits, level, medal, 1, &info);
            }
            if (found == count) return bits;
        }
    }
    return bits;
}

// [decomp] 0x00200238 Menu_SpecialCodenameCheck
// O código digitado em "Secret Unlocks": se é um dos 52, põe as missões liberadas da entrada (0: só as duas
// primeiras) em sp_level e os bônus dela no jogador 0 (Menu_SetBonus) e devolve 1; senão 0. As entradas são calculadas
// do estado atual a cada chamada, como o original as monta na pilha: por isso os códigos de "ou exclusivo" desfazem
// quando digitados de novo.
uint32_t Menu_SpecialCodenameCheck(char* code) {
    const uint64_t b = Menu_GetBonus(0);
    uint32_t m = 0;
    for (uint32_t i = 0; i < 12; ++i) {
        if (sp_items()[i].enabled != 0) m |= 1u << i;
    }
    uint64_t up[12];
    for (uint32_t i = 0; i < 12; ++i) up[i] = Menu_UpgradeCheat(b, kUpgrades[i].bit, kUpgrades[i].count);
    // "Q LAB": todas as melhorias de uma vez, menos a primeira, a quarta e a quinta chamada.
    const uint64_t all = up[1] | up[2] | up[5] | up[6] | up[7] | up[8] | up[9] | up[10] | up[11];
    for (const SecretCode& e : kCodes) {
        if (std::strcmp(code, e.code) != 0) continue;
        uint32_t missions = m;
        if (e.missions == Missions::All) missions = 0xffffffffu;
        if (e.missions == Missions::Toggle) missions = m ^ e.mission_bits;
        uint64_t bonus = b;
        if (e.bonus == Bonus::Xor) bonus = b ^ e.bonus_bits;
        if (e.bonus == Bonus::Or) bonus = b | e.bonus_bits;
        if (e.bonus == Bonus::Upgrade) bonus = up[e.bonus_bits];
        if (e.bonus == Bonus::Upgrades) bonus = all;
        if (missions == 0) missions = 3;
        for (uint32_t i = 0; i < 12; ++i) sp_items()[i].enabled = (missions & (1u << i)) != 0 ? 1 : 0;
        Menu_SetBonus(bonus, 0, 0);
        return 1;
    }
    return 0;
}

// Só no port: os códigos, na ordem do original (nf_loader_check os confere com o ELF).
const char* Menu_PortSecretCode(uint32_t i) { return i < sizeof(kCodes) / sizeof(kCodes[0]) ? kCodes[i].code : nullptr; }
