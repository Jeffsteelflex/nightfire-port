#include "game/static_init.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "engine/copter.h"
#include "engine/drone.h"
#include "engine/effect.h"
#include "engine/font.h"
#include "engine/gunimp.h"
#include "engine/loadsave.h"
#include "engine/menu.h"
#include "engine/menu_dossier.h"
#include "engine/menu_frontend.h"
#include "engine/pickup.h"
#include "engine/player.h"
#include "engine/player_stats.h"
#include "engine/script.h"
#include "engine/sensor.h"
#include "engine/sound.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/vecmath.h"
#include "engine/weapons.h"
#include "platform/psi_card.h"

namespace nf {

namespace {

constexpr uint8_t kFunc = 2;
constexpr uint8_t kObject = 1;
constexpr uint32_t kNoField = 0xffffffffu;

// Tabelas que o port copia da imagem depois dos construtores: (símbolo, destino, tamanho), mais o
// tamanho de cada entrada, o offset de até dois ponteiros de código por entrada, o de um ponteiro de
// dados que o port não usa (zerado na cópia: um endereço do PS2 não serve aqui) e o de um ponteiro de
// dados para dentro de outra tabela copiada (ref_symbol), traduzido para o lugar correspondente na
// cópia. Com end_symbol, a "tabela" é a faixa do símbolo até o início de end_symbol. ref_field2 e
// ref_symbol2: um segundo ponteiro desses; ref_symbol "*" aceita qualquer tabela de kTables que contenha
// o endereço (FontTable aponta para três fontes diferentes).
struct TableCopy {
    const char* symbol;
    uint8_t* dst;
    uint32_t size;
    uint32_t stride;
    uint32_t code_field;
    uint32_t data_field = 0xffffffffu;
    uint32_t code_field2 = 0xffffffffu;
    uint32_t ref_field = 0xffffffffu;
    const char* ref_symbol = nullptr;
    const char* end_symbol = nullptr;
    uint32_t ref_field2 = 0xffffffffu;
    const char* ref_symbol2 = nullptr;
};

template <class T>
uint8_t* bytes_of(T* p) {
    return reinterpret_cast<uint8_t*>(p);
}

static_assert(sizeof(weapon_data) == kWeaponDataSize && sizeof(ammo_data) == kAmmoDataSize);

const TableCopy kTables[] = {
    {"weapon_data", weapon_data, kWeaponDataSize, kWeaponStride, kWeaponCallbackOffset},
    {"ammo_data", ammo_data, kAmmoDataSize, kAmmoStride, kNoField},
    // +0: ponteiro para o nome do material ("CARPET"...), não usado pelos efeitos.
    {"EffectInfo", reinterpret_cast<uint8_t*>(EffectInfo), kEffectInfoSize, sizeof(HEINFO_tag), kNoField, 0},
    // Personagens da IA (engine/drone.h).
    {"DroneTypeSettings", bytes_of(DroneTypeSettings), sizeof(DroneTypeSettings), sizeof(DroneTypeSetting), 4, kNoField,
     8},
    {"DroneModeSettings", bytes_of(DroneModeSettings), sizeof(DroneModeSettings), sizeof(DroneModeSetting), 8},
    {"Drone_AnimInfo", bytes_of(Drone_AnimInfo), sizeof(Drone_AnimInfo), sizeof(DroneAnimInfo), kNoField},
    {"Drone_AnimTables", bytes_of(Drone_AnimTables), sizeof(Drone_AnimTables), sizeof(Drone_AnimTables[0]), kNoField},
    // As listas DASC_* de cada estado de animação, contíguas antes de DroneAnimStates.
    {"DASC_AAA_Undefined", DroneAnimStateCalls, kDroneAnimCallsSize, 6, kNoField, kNoField, kNoField, kNoField, nullptr,
     "DroneAnimStates"},
    {"DroneAnimStates", bytes_of(DroneAnimStates), sizeof(DroneAnimStates), sizeof(DroneAnimState), kNoField, kNoField,
     kNoField, 8, "DASC_AAA_Undefined"},
    {"DroneData_AnimFunc", bytes_of(DroneData_AnimFunc), sizeof(DroneData_AnimFunc), sizeof(DroneDataFunc), 4},
    {"DroneData_DAStateFunc", bytes_of(DroneData_DAStateFunc), sizeof(DroneData_DAStateFunc), sizeof(DroneDataFunc), 4},
    {"NDrone2_StateFuncs", bytes_of(NDrone2_StateFuncs), sizeof(NDrone2_StateFuncs), 4, 0},
    {"bitDescs", bitDescs, sizeof(bitDescs), 2, kNoField},
    {"bitMasks", bytes_of(bitMasks), sizeof(bitMasks), 4, kNoField},
    {"DroneCaptain_Mod_BulletDamage", bytes_of(&DroneCaptain_Mod_BulletDamage), 4, 4, kNoField},
    // Helicópteros (engine/copter.h): o dano por peça. COPT_COS/COPT_SIN (símbolos sem tipo no ELF) são o construtor
    // estático de copter.cpp.
    {"Bits", bytes_of(Bits), sizeof(Bits), sizeof(COPTERBIT), kNoField},
    // O sistema de segurança (engine/sensor.h): as falas das contagens e os segundos delas; as armas dos canhões
    // montados (engine/gunimp.h).
    {"Laser1.134", bytes_of(Laser1_134), sizeof(Laser1_134), sizeof(SSYSMSG), kNoField},
    {"Laser2.135", bytes_of(Laser2_135), sizeof(Laser2_135), sizeof(SSYSMSG), kNoField},
    {"Cam1.132", bytes_of(Cam1_132), sizeof(Cam1_132), sizeof(SSYSMSG), kNoField},
    {"Cam2.133", bytes_of(Cam2_133), sizeof(Cam2_133), sizeof(SSYSMSG), kNoField},
    {"MIN3", bytes_of(SSysMinutes), sizeof(SSysMinutes), 4, kNoField, kNoField, kNoField, kNoField, nullptr,
     "SSysDelayTimer"},
    {"TurretGuns", bytes_of(TurretGuns), sizeof(TurretGuns), sizeof(TURRETGUN), kNoField},
    {"DroneCaptain_Mod_BulletAccuracy", bytes_of(&DroneCaptain_Mod_BulletAccuracy), 4, 4, kNoField},
    {"DroneCaptain_Mod_Health", bytes_of(&DroneCaptain_Mod_Health), 4, 4, kNoField},
    // Dano que os personagens recebem (engine/drone_damage.cpp) e as falas deles (drone_vision.cpp).
    {"DroneDamage_Easy", bytes_of(&DroneDamage_Easy), 4, 4, kNoField},
    {"DroneDamage_Normal", bytes_of(&DroneDamage_Normal), 4, 4, kNoField},
    {"DroneDamage_Hard", bytes_of(&DroneDamage_Hard), 4, 4, kNoField},
    {"DroneDamage_Head", bytes_of(&DroneDamage_Head), 4, 4, kNoField},
    {"DroneDamage_Legs", bytes_of(&DroneDamage_Legs), 4, 4, kNoField},
    {"DroneDamage_Arms", bytes_of(&DroneDamage_Arms), 4, 4, kNoField},
    {"DroneDamage_Torso", bytes_of(&DroneDamage_Torso), 4, 4, kNoField},
    {"DroneArmour_Helmet", bytes_of(&DroneArmour_Helmet), 4, 4, kNoField},
    {"DroneArmour_Combat", bytes_of(&DroneArmour_Combat), 4, 4, kNoField},
    {"DroneArmour_Jacket", bytes_of(&DroneArmour_Jacket), 4, 4, kNoField},
    {"DroneArmour_Vest", bytes_of(&DroneArmour_Vest), 4, 4, kNoField},
    {"Drone_SfxInfo", bytes_of(Drone_SfxInfo), sizeof(Drone_SfxInfo), sizeof(DroneSfxInfo), kNoField},
    // Os avisos dos alto-falantes de 0700000c/0d (NDrone2_PowerstationTalk): {canal 0c, canal 0d, fala}.
    {"PowerSfx", bytes_of(PowerSfx), sizeof(PowerSfx), sizeof(PowerSfxEntry), kNoField},
    {"Drone_View_Bones", bytes_of(Drone_View_Bones), sizeof(Drone_View_Bones), 4, kNoField},
    // Constantes de tiro da IA (engine/drone_weapon.cpp): DroneFiring_BurstDelay_Min até
    // DroneFiring_PlayerBackShot_Damage, contíguas antes de DroneCaptain_Mod_BulletDamage.
    {"DroneFiring_BurstDelay_Min", bytes_of(&DroneFiring), sizeof(DroneFiring), 4, kNoField, kNoField, kNoField,
     kNoField, nullptr, "DroneCaptain_Mod_BulletDamage"},
    // Caixas das juntas dos personagens (AnimGetCollData, engine/drone_move.cpp).
    {"HumanCollisionBones", bytes_of(HumanCollisionBones), sizeof(HumanCollisionBones), sizeof(AnimCollBone),
     kNoField},
    {"AstronautCollisionBones", bytes_of(AstronautCollisionBones), sizeof(AstronautCollisionBones),
     sizeof(AnimCollBone), kNoField},
    // Animações de cobertura (DroneAnim_CoverAnim, engine/drone_combat.cpp): os vetores CCMode*, contíguos
    // até CCModes, a tabela de ponteiros para eles e o da cobertura baixa (LCModes aponta para ele).
    {"CCModeCrouchLeanL", bytes_of(CCModeTables), sizeof(CCModeTables), 2, kNoField, kNoField, kNoField, kNoField, nullptr,
     "CCModes"},
    {"CCModes", bytes_of(CCModes), sizeof(CCModes), 4, kNoField, kNoField, kNoField, 0, "CCModeCrouchLeanL"},
    {"LCModeCrouch", bytes_of(LCModeCrouch), sizeof(LCModeCrouch), 2, kNoField},
    // Dano que o jogador recebe (Player_HandlePain, engine/player.cpp).
    {"Plr_DMod_Multi", bytes_of(&Plr_DMod_Multi), 4, 4, kNoField},
    {"Plr_DMod_Head", bytes_of(&Plr_DMod_Head), 4, 4, kNoField},
    {"Plr_DMod_LowerLimb", bytes_of(&Plr_DMod_LowerLimb), 4, 4, kNoField},
    {"Plr_DMod_UpperLimb", bytes_of(&Plr_DMod_UpperLimb), 4, 4, kNoField},
    {"Plr_DMod_Easy", bytes_of(&Plr_DMod_Easy), 4, 4, kNoField},
    {"Plr_DMod_Normal", bytes_of(&Plr_DMod_Normal), 4, 4, kNoField},
    {"Plr_DMod_Hard", bytes_of(&Plr_DMod_Hard), 4, 4, kNoField},
    // Conjuntos de armas dos itens do multijogador e as armas do sorteio (engine/pickup.cpp).
    {"PickupMatrix", bytes_of(PickupMatrix), sizeof(PickupMatrix), 2, kNoField},
    {"UseableGuns", bytes_of(UseableGuns), sizeof(UseableGuns), 2, kNoField},
    // Fontes (engine/font.h): os pares de kerning, os glifos (+0x10 aponta os pares do glifo), as três
    // fontes (+0x04 os pares, +0x10 os glifos), FontTable (+0x04 uma das fontes) e os símbolos de botão.
    {"KernNFont2", bytes_of(KernNFont2), sizeof(KernNFont2), 2, kNoField},
    {"KernMedium", bytes_of(KernMedium), sizeof(KernMedium), 2, kNoField},
    {"KernSerpLight", bytes_of(KernSerpLight), sizeof(KernSerpLight), 2, kNoField},
    {"FontDataNFont2", bytes_of(FontDataNFont2), sizeof(FontDataNFont2), sizeof(CHARDATA_tag), kNoField, kNoField,
     kNoField, 0x10, "KernNFont2"},
    {"FontDataMedium", bytes_of(FontDataMedium), sizeof(FontDataMedium), sizeof(CHARDATA_tag), kNoField, kNoField,
     kNoField, 0x10, "KernMedium"},
    {"FontDataSerpLight", bytes_of(FontDataSerpLight), sizeof(FontDataSerpLight), sizeof(CHARDATA_tag), kNoField,
     kNoField, kNoField, 0x10, "KernSerpLight"},
    {"FONTNFont2", bytes_of(&FONTNFont2), sizeof(FONT_tag), sizeof(FONT_tag), kNoField, kNoField, kNoField, 4,
     "KernNFont2", nullptr, 0x10, "FontDataNFont2"},
    {"FONTMedium", bytes_of(&FONTMedium), sizeof(FONT_tag), sizeof(FONT_tag), kNoField, kNoField, kNoField, 4,
     "KernMedium", nullptr, 0x10, "FontDataMedium"},
    {"FONTSerpLight", bytes_of(&FONTSerpLight), sizeof(FONT_tag), sizeof(FONT_tag), kNoField, kNoField, kNoField, 4,
     "KernSerpLight", nullptr, 0x10, "FontDataSerpLight"},
    {"FontTable", bytes_of(FontTable), sizeof(FontTable), sizeof(FONTTABLE_tag), kNoField, kNoField, kNoField, 4, "*"},
    {"specialchar", bytes_of(specialchar), sizeof(specialchar), sizeof(SPECIALCHAR_tag), kNoField},
    // A tarja de cinema dos scripts de cena (engine/script.cpp).
    {"BorderInfo", bytes_of(&BorderInfo), sizeof(SpriteInfo), sizeof(SpriteInfo), kNoField},
    // Legendas das falas (Sound_DoSubtitle, engine/text.cpp): som -> rótulo, a tabela dos sons (duração,
    // alcance) e o fator de duração.
    {"Snd2Lbl", bytes_of(Snd2Lbl), sizeof(Snd2Lbl), sizeof(SND2LBL_tag), kNoField},
    {"SFXOutputData", SFXOutputData, sizeof(SFXOutputData), 0x18, kNoField},
    {"g_fSubtitleDurationModification", bytes_of(&g_fSubtitleDurationModification), 4, 4, kNoField},
    // A música (UpdateMusicalEvents, engine/sound.cpp): a faixa e os marcadores de cada fase e o estado de música
    // da IA por fase (MusicVars).
    {"MapMusic", bytes_of(MapMusic), sizeof(MapMusic), 0x4c, kNoField},
    {"MapDroneData", bytes_of(MapDroneData), sizeof(MapDroneData), 0x10, kNoField},
    // Menus (engine/menu.h): os textos de ajuda de cada controle, o sprite do cursor (+0x20 o formato,
    // nulo), as medalhas de cada fase e a versão (página de trapaças).
    {"help_table", bytes_of(help_table), sizeof(help_table), 12, kNoField},
    {"CursorInfo", bytes_of(&CursorInfo), sizeof(SpriteInfo), sizeof(SpriteInfo), kNoField, 0x20},
    {"RewardsTable", RewardsTable, sizeof(RewardsTable), 0x40, kNoField},
    {"VersionString", bytes_of(VersionString), sizeof(VersionString), sizeof(VersionString), kNoField},
    // O menu principal (engine/menu_frontend.h): o perfil em uso e os dois padrões, as opções do codinome e as
    // dificuldades (M_ITEM), os blocos do perfil gravado (engine/loadsave.h: +8 monta, +0xc lê) e as tabelas
    // do cartão (platform/psi_card.h: o título do ícone em Shift-JIS e o fundo do icon.sys).
    {"ls", ls, sizeof(ls), sizeof(ls), kNoField},
    {"def_codename", def_codename, sizeof(def_codename), 0x38, kNoField},
    {"cn_options", bytes_of(cn_options), sizeof(cn_options), sizeof(M_ITEM), kNoField},
    {"difficulty", bytes_of(difficulty), sizeof(difficulty), sizeof(M_ITEM), kNoField},
    // O dossiê (engine/menu_dossier.h): as quatro seções, as armas e os aparelhos (M_ITEM).
    {"ds_options", bytes_of(ds_options), sizeof(ds_options), sizeof(M_ITEM), kNoField},
    {"ds_weapons", bytes_of(ds_weapons), sizeof(ds_weapons), sizeof(M_ITEM), kNoField},
    {"ds_gadgets", bytes_of(ds_gadgets), sizeof(ds_gadgets), sizeof(M_ITEM), kNoField},
    {"SaveIFFBlocks", bytes_of(SaveIFFBlocks), sizeof(SaveIFFBlocks), sizeof(SAVEIFFBLOCK), 8, kNoField, 0xc},
    {"NumSaveBlocks", bytes_of(&NumSaveBlocks), 4, 4, kNoField},
    {"ascii_table", ascii_table, sizeof(ascii_table), 4, kNoField},
    {"ascii_special", ascii_special, sizeof(ascii_special), 4, kNoField},
    {"bgcolor", bgcolor, sizeof(bgcolor), 4, kNoField},
    {"ambient", ambient, sizeof(ambient), 4, kNoField},
    // A pontuação das missões (engine/player_stats.h): as 9 linhas de cada uma (HT_Level_*_ST, uma a cada 0xb8
    // bytes até ScoringTable), a tabela (+0x18 aponta as linhas da missão) e os multiplicadores do bônus por
    // dificuldade.
    {"HT_Level_Driving_Paris_ST", bytes_of(ScoringRows), sizeof(ScoringRows), sizeof(SCORETABLE_ROWS), kNoField, kNoField,
     kNoField, kNoField, nullptr, "ScoringTable"},
    {"ScoringTable", bytes_of(ScoringTable), sizeof(ScoringTable), sizeof(SCORETABLE), kNoField, kNoField, kNoField, 0x18,
     "HT_Level_Driving_Paris_ST"},
    {"ScoringTableDifMul", bytes_of(ScoringTableDifMul), sizeof(ScoringTableDifMul), 4, kNoField},
};

std::string hex8(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08x", v);
    return buf;
}

std::string hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%x", v);
    return buf;
}

// "Nome+0x10" para mensagens.
std::string describe(const Elf& elf, uint32_t addr) {
    const ElfSymbol* s = elf.symbol_at(addr, kFunc);
    if (!s) s = elf.symbol_at(addr, kObject);
    if (!s) return hex8(addr);
    return addr == s->value ? s->name : s->name + "+" + hex(addr - s->value);
}

const ElfSection* section_at(const Elf& elf, uint32_t addr) {
    for (const ElfSection& s : elf.sections()) {
        if (s.addr != 0 && (s.flags & 2) != 0 && addr >= s.addr && addr - s.addr < s.size) return &s;  // SHF_ALLOC
    }
    return nullptr;
}

std::string with_symbol(const std::string& what, const Elf& elf, const MipsError& e) {
    return what + ": " + e.what() + " [" + describe(elf, e.pc) + "]";
}

// Símbolos de dados ordenados, para atribuir bytes escritos a uma variável.
class DataSymbols {
public:
    explicit DataSymbols(const Elf& elf) {
        for (const ElfSymbol& s : elf.symbols()) {
            if ((s.type != kObject && s.type != 0) || s.name.empty() || s.name == "gcc2_compiled.") continue;
            const ElfSection* sec = section_at(elf, s.value);
            if (!sec || (sec->flags & 4) != 0) continue;  // fora da imagem ou código (SHF_EXECINSTR)
            syms_.push_back(&s);
        }
        std::sort(syms_.begin(), syms_.end(), [](const ElfSymbol* a, const ElfSymbol* b) {
            return a->value != b->value ? a->value < b->value : a->size < b->size;
        });
    }

    std::string name(uint32_t addr, const Elf& elf) const {
        auto it = std::upper_bound(syms_.begin(), syms_.end(), addr,
                                   [](uint32_t a, const ElfSymbol* s) { return a < s->value; });
        if (it != syms_.begin()) {
            const ElfSymbol* s = *(it - 1);
            if (s->size == 0 || addr - s->value < s->size) return s->name;
        }
        const ElfSection* sec = section_at(elf, addr);
        return (sec ? sec->name : std::string("?")) + "@" + hex8(addr);
    }

private:
    std::vector<const ElfSymbol*> syms_;
};

// String C que começa exatamente em v (byte anterior zero, 3+ caracteres imprimíveis e o zero final).
std::string string_at(const MipsMachine& m, uint32_t v) {
    if (v == 0 || v >= MipsMachine::kRamSize || m.ram()[v - 1] != 0) return {};
    std::string str;
    for (uint32_t a = v; a < MipsMachine::kRamSize && str.size() < 64; ++a) {
        const char c = char(m.ram()[a]);
        if (c == 0) return str.size() >= 3 ? str : std::string();
        if (c < 0x20 || c > 0x7e) return {};
        str += c;
    }
    return {};
}

// Se a palavra v parece um ponteiro para dentro da imagem do ELF (o início exato de uma função ou de
// uma variável, ou o começo de uma string), o texto que a descreve; senão vazio. Campos pequenos
// empacotados (dois int16, por exemplo) caem no meio de funções e variáveis e não contam.
std::string pointer_like(const Elf& elf, const MipsMachine& m, uint32_t v) {
    const ElfSection* sec = section_at(elf, v);
    if (!sec) return {};
    const ElfSymbol* fn = elf.symbol_at(v, kFunc);
    if (fn && fn->value == v) return "função " + fn->name;
    const ElfSymbol* obj = elf.symbol_at(v, kObject);
    if (obj && obj->value == v) return "variável " + obj->name + " (" + sec->name + ")";
    if ((sec->flags & 4) == 0) {
        const std::string str = string_at(m, v);
        if (!str.empty()) return "string \"" + str + "\" (" + sec->name + ")";
    }
    return {};
}

}  // namespace

std::vector<uint32_t> static_constructors(const Elf& elf) {
    // crtstuff do GCC: __CTOR_LIST__ = { n ou -1, f1, ..., fn, 0 }; __do_global_ctors chama de fn a f1.
    const auto list = elf.find("__CTOR_LIST__");
    if (!list) throw FormatError("símbolo __CTOR_LIST__ ausente no ELF");
    const ByteView d = elf.at(list->value, list->size);
    std::vector<uint32_t> fns;
    for (uint32_t o = 4; o + 4 <= d.size() && u32(d, o) != 0; o += 4) fns.push_back(u32(d, o));
    const uint32_t count = u32(d, 0);
    if (count != 0xffffffffu && count != fns.size()) throw FormatError("__CTOR_LIST__ com contagem inesperada");
    std::reverse(fns.begin(), fns.end());
    return fns;
}

void setup_constructor_machine(MipsMachine& m, const Elf& elf) {
    m.load(elf);
    for (const ElfSymbol& s : elf.symbols()) {
        if (s.type != kFunc || s.size == 0) continue;
        if (s.name == "__do_global_ctors" || s.name == "__static_initialization_and_destruction_0" ||
            s.name.rfind("_GLOBAL_$I$", 0) == 0) {
            m.allow_code(s.value, s.value + s.size);
        }
    }
    auto need = [&](const char* name) {
        const auto s = elf.find(name);
        if (!s || s->type != kFunc) throw FormatError(std::string("função ") + name + " ausente no ELF");
        return s->value;
    };
    // memset(dst, c, n) e memcpy(dst, src, n) da newlib: devolvem dst.
    m.hook(need("memset"), "memset", [](MipsMachine& mm) {
        const uint32_t dst = uint32_t(mm.gpr(MipsMachine::a0)), n = uint32_t(mm.gpr(MipsMachine::a2));
        std::memset(mm.mem(dst, n), int(mm.gpr(MipsMachine::a1) & 0xff), n);
        mm.set_gpr(MipsMachine::v0, mm.gpr(MipsMachine::a0));
    });
    m.hook(need("memcpy"), "memcpy", [](MipsMachine& mm) {
        const uint32_t dst = uint32_t(mm.gpr(MipsMachine::a0)), src = uint32_t(mm.gpr(MipsMachine::a1));
        const uint32_t n = uint32_t(mm.gpr(MipsMachine::a2));
        std::memmove(mm.mem(dst, n), mm.mem(src, n), n);
        mm.set_gpr(MipsMachine::v0, mm.gpr(MipsMachine::a0));
    });
    // float PS2Sinf(float): f12 -> f0 (o construtor de Copter calcula COPT_COS/COPT_SIN com ele).
    m.hook(need("PS2Sinf__Ff"), "PS2Sinf", [](MipsMachine& mm) { mm.set_fpr(0, PS2Sinf(mm.fpr(12))); });
}

bool init_game_tables(const Elf& elf, CodeResolver resolve_code, std::string* error, std::vector<std::string>* log) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    try {
        MipsMachine m;
        setup_constructor_machine(m, elf);
        const auto ctors = elf.find("__do_global_ctors");
        if (!ctors || ctors->type != kFunc) return fail("função __do_global_ctors ausente no ELF");
        try {
            m.call(ctors->value);
        } catch (const MipsError& e) {
            return fail(with_symbol("construtores estáticos", elf, e));
        }
        if (uint32_t(m.gpr(MipsMachine::sp)) != MipsMachine::kStackTop) return fail("construtores desbalancearam a pilha");

        for (const TableCopy& t : kTables) {
            const auto sym = elf.find(t.symbol);
            if (!sym || sym->type != kObject) return fail(std::string("símbolo ") + t.symbol + " ausente no ELF");
            uint32_t sym_size = sym->size;
            if (t.end_symbol != nullptr) {
                const auto end = elf.find(t.end_symbol);
                if (!end || end->value < sym->value) return fail(std::string("símbolo ") + t.end_symbol + " ausente no ELF");
                sym_size = end->value - sym->value;
            }
            if (sym_size != t.size) {
                return fail(std::string("símbolo ") + t.symbol + " tem " + std::to_string(sym_size) +
                            " bytes no ELF; o port espera " + std::to_string(t.size));
            }
            // Ponteiros ref_field/ref_field2: o endereço do PS2 dentro de outra tabela copiada vira o lugar
            // correspondente na cópia dela.
            const auto translate = [&](const char* ref, uint32_t v, uint32_t& port) {
                for (const TableCopy& r : kTables) {
                    if (std::strcmp(ref, "*") != 0 && std::strcmp(r.symbol, ref) != 0) continue;
                    const auto rs = elf.find(r.symbol);
                    if (rs && v >= rs->value && v - rs->value < r.size) {
                        port = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r.dst + (v - rs->value)));
                        return true;
                    }
                }
                return false;
            };
            for (const char* ref : {t.ref_symbol, t.ref_symbol2}) {
                if (ref == nullptr || std::strcmp(ref, "*") == 0) continue;
                bool known = false;
                for (const TableCopy& r : kTables) known = known || std::strcmp(r.symbol, ref) == 0;
                if (!known || !elf.find(ref)) return fail(std::string("tabela ") + ref + " desconhecida");
            }
            int refs = 0;
            std::vector<uint8_t> bytes(m.mem(sym->value, t.size), m.mem(sym->value, t.size) + t.size);
            int code_pointers = 0, suspects = 0;
            for (uint32_t o = 0; o + 4 <= t.size; o += 4) {
                uint32_t v;
                std::memcpy(&v, bytes.data() + o, 4);
                const uint32_t entry = o / t.stride, field = o % t.stride;
                const std::string where = std::string(t.symbol) + "[" + std::to_string(entry) + "]+" + hex(field);
                if (field == t.data_field) {
                    std::memset(bytes.data() + o, 0, 4);
                    continue;
                }
                if (field == t.ref_field || field == t.ref_field2) {
                    if (v == 0) continue;
                    const char* ref = field == t.ref_field ? t.ref_symbol : t.ref_symbol2;
                    uint32_t port = 0;
                    if (!translate(ref, v, port)) return fail(where + " = " + hex8(v) + " fora de " + ref);
                    std::memcpy(bytes.data() + o, &port, 4);
                    ++refs;
                    continue;
                }
                if (field == t.code_field || field == t.code_field2) {
                    if (v == 0) continue;
                    // Ponteiro de código: nunca copia o endereço do PS2, traduz pelo símbolo.
                    const ElfSymbol* fn = elf.symbol_at(v, kFunc);
                    if (!fn || fn->value != v) return fail(where + " = " + hex8(v) + " não é o início de uma função");
                    const uint32_t port = resolve_code ? resolve_code(fn->name.c_str(), v) : 0;
                    std::memcpy(bytes.data() + o, &port, 4);
                    ++code_pointers;
                    if (log) log->push_back(where + " = " + hex8(v) + " " + fn->name + " -> " + (port ? hex8(port) : "não portada"));
                } else if (const std::string p = pointer_like(elf, m, v); !p.empty()) {
                    // Outro ponteiro para dentro do ELF seria um endereço do PS2 sem sentido no port.
                    ++suspects;
                    if (log) log->push_back("aviso: " + where + " = " + hex8(v) + " parece ponteiro: " + p);
                }
            }
            if (log) {
                std::string line = std::string(t.symbol) + ": " + std::to_string(t.size) + " bytes de " + hex8(sym->value);
                if (t.code_field != kNoField) {
                    line += ", " + std::to_string(code_pointers) + " ponteiros de código em +" + hex(t.code_field);
                    if (t.code_field2 != kNoField) line += " e +" + hex(t.code_field2);
                }
                if (t.ref_symbol != nullptr) {
                    line += ", " + std::to_string(refs) + " ponteiros para " + t.ref_symbol;
                    if (t.ref_symbol2 != nullptr) line += std::string(" e ") + t.ref_symbol2;
                }
                line += ", " + std::to_string(suspects) + " outras palavras parecidas com ponteiro";
                log->push_back(line);
            }
            std::memcpy(t.dst, bytes.data(), t.size);
        }
        return true;
    } catch (const MipsError& e) {
        return fail(with_symbol("interpretador MIPS", elf, e));
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

std::vector<ConstructorEffect> survey_static_constructors(const Elf& elf) {
    std::vector<ConstructorEffect> out;
    MipsMachine m;
    setup_constructor_machine(m, elf);
    const std::vector<uint8_t> initial = m.ram();
    const DataSymbols names(elf);
    const uint32_t stack_bottom = MipsMachine::kStackTop - 0x100000;  // pilha dos construtores: não conta
    for (const uint32_t fn : static_constructors(elf)) {
        ConstructorEffect e;
        e.address = fn;
        e.name = describe(elf, fn);
        setup_constructor_machine(m, elf);
        try {
            m.call(fn);
        } catch (const MipsError& err) {
            e.error = with_symbol(e.name, elf, err);
        }
        std::map<std::string, uint32_t> bytes;
        std::vector<std::string> order;
        constexpr uint32_t kPage = 0x1000;
        for (uint32_t p = 0; p < stack_bottom; p += kPage) {
            if (std::memcmp(m.ram().data() + p, initial.data() + p, kPage) == 0) continue;
            for (uint32_t a = p; a < p + kPage; ++a) {
                if (m.ram()[a] == initial[a]) continue;
                const std::string n = names.name(a, elf);
                if (bytes[n]++ == 0) order.push_back(n);
            }
        }
        for (const std::string& n : order) e.writes.push_back({n, bytes[n]});
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace nf
