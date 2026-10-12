#include "game/menu_tables.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "engine/menu.h"
#include "engine/menu_frontend.h"
#include "engine/menu_tweaks.h"
#include "engine/mission.h"
#include "game/static_init.h"

namespace nf {

namespace {

constexpr uint8_t kFunc = 2;
constexpr uint32_t kLabelMark = 0x80000000u;  // Txt_BindLabel devolve o rótulo marcado (fora da RAM do PS2)
constexpr uint32_t kCountAddr = 0x01700000u;  // o uint32 que recebe a contagem
constexpr uint32_t kTextAddr = 0x01700100u;   // o código de "Secret Unlocks" para Menu_SpecialCodenameCheck
constexpr uint32_t kControlAddr = 0x01700200u;  // um controle falso para os tratadores das listas
constexpr uint32_t kHeapAddr = 0x01710000u;   // o que Menu_Malloc devolve
constexpr uint32_t kLineSize = 12;

uint32_t function(const Elf& elf, const char* name) {
    const auto s = elf.find(name);
    if (!s || s->type != kFunc) throw std::runtime_error(std::string("função ") + name + " ausente no ELF");
    return s->value;
}
uint32_t object(const Elf& elf, const char* name) {
    const auto s = elf.find(name);
    if (!s) throw std::runtime_error(std::string("símbolo ") + name + " ausente no ELF");
    return s->value;
}
void allow(MipsMachine& m, const Elf& elf, const char* name) {
    const auto s = elf.find(name);
    if (!s || s->type != kFunc) throw std::runtime_error(std::string("função ") + name + " ausente no ELF");
    m.allow_code(s->value, s->value + s->size);
}
std::string c_string(const MipsMachine& m, uint32_t addr) {
    std::string s;
    for (uint32_t a = addr; a < MipsMachine::kRamSize && s.size() < 0x100; ++a) {
        const char c = char(m.ram()[a]);
        if (c == 0) break;
        s.push_back(c);
    }
    return s;
}

// As strings copiadas uma vez por endereço do ELF.
class Strings {
public:
    explicit Strings(const MipsMachine& m) : m_(m) {}
    const char* at(uint32_t addr) {
        auto it = copies_.find(addr);
        if (it != copies_.end()) return it->second;
        const char* copy = Menu_PortTableString(c_string(m_, addr).c_str());
        if (copy == nullptr) throw std::runtime_error("os textos das tabelas do menu não cabem no espaço do port");
        copies_.emplace(addr, copy);
        return copy;
    }
    size_t count() const { return copies_.size(); }

private:
    const MipsMachine& m_;
    std::map<uint32_t, const char*> copies_;
};

// Os créditos (Menu_SetupCredits).
void import_credits(const Elf& elf, Strings& strings, std::vector<std::string>* log) {
    MipsMachine m;
    setup_constructor_machine(m, elf);  // a imagem do ELF, memset e memcpy nativos
    allow(m, elf, "Menu_SetupCredits__FPUi");
    uint32_t labels = 0;
    m.hook(function(elf, "Txt_BindLabel__FUiSc"), "Txt_BindLabel", [&labels](MipsMachine& mm) {
        ++labels;
        mm.set_gpr(MipsMachine::v0, kLabelMark | (uint32_t(mm.gpr(MipsMachine::a0)) & 0x7fffffffu));
    });
    m.hook(function(elf, "Menu_Malloc__FUi14MENUMALLOCTYPE"), "Menu_Malloc",
           [](MipsMachine& mm) { mm.set_gpr(MipsMachine::v0, kHeapAddr); });
    m.hook(function(elf, "amemcpy__FPvT0Ui"), "amemcpy", [](MipsMachine& mm) {
        const uint32_t dst = uint32_t(mm.gpr(MipsMachine::a0)), src = uint32_t(mm.gpr(MipsMachine::a1));
        const uint32_t n = uint32_t(mm.gpr(MipsMachine::a2));
        std::memmove(mm.mem(dst, n), mm.mem(src, n), n);
    });
    // ptr_credits começa nulo (o .bss): a função aloca e copia.
    m.call(function(elf, "Menu_SetupCredits__FPUi"), {kCountAddr});
    const uint32_t lines = uint32_t(m.gpr(MipsMachine::v0));
    const uint32_t count = m.read32(kCountAddr);
    if (lines != kHeapAddr) throw std::runtime_error("Menu_SetupCredits não devolveu as linhas copiadas");
    if (count != uint32_t(kCreditsMax)) {
        throw std::runtime_error("Menu_SetupCredits devolveu " + std::to_string(count) + " linhas; o port espera " +
                                 std::to_string(kCreditsMax));
    }
    const size_t before = strings.count();
    const auto text = [&](uint32_t v, uint32_t* label, const char** out) {
        *label = 0;
        *out = nullptr;
        if (v == 0) return;
        if ((v & kLabelMark) != 0) {
            *label = v & ~kLabelMark;
            return;
        }
        *out = strings.at(v);
    };
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = m.mem(kHeapAddr + i * kLineSize, kLineSize);
        uint32_t name = 0, title = 0;
        std::memcpy(&name, e, 4);
        std::memcpy(&title, e + 4, 4);
        CREDIT_SOURCE& s = CreditsSource[i];
        text(name, &s.name_label, &s.name_text);
        text(title, &s.title_label, &s.title_text);
        s.name_font = e[8];
        s.title_font = e[9];
        s.single = e[10];
    }
    CreditsSourceCount = count;
    if (log) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "créditos: %u linhas, %u rótulos do texto, %zu strings do ELF", count, labels,
                      strings.count() - before);
        log->push_back(buf);
    }
}

// Os itens que um tratador de lista acrescenta ao ser criado (0x51: __Menu_SendMessage(c, 0x10, texto, valor)).
uint32_t import_list(const Elf& elf, Strings& strings, const char* handler, MENU_LIST_ITEM* out, uint32_t max) {
    MipsMachine m;
    setup_constructor_machine(m, elf);
    allow(m, elf, handler);
    uint32_t count = 0;
    m.hook(function(elf, "__Menu_SendMessage__FP9M_CONTROLUiii"), "__Menu_SendMessage", [&](MipsMachine& mm) {
        mm.set_gpr(MipsMachine::v0, 0);
        if (uint32_t(mm.gpr(MipsMachine::a1)) != 0x10) return;
        if (count >= max) throw std::runtime_error(std::string(handler) + ": itens demais para o port");
        out[count].text = strings.at(uint32_t(mm.gpr(MipsMachine::a2)));
        out[count].value = int32_t(uint32_t(mm.gpr(MipsMachine::a3)));
        ++count;
    });
    m.call(function(elf, handler), {0, kControlAddr, 0, 0x51});
    return count;
}

}  // namespace

bool init_menu_tables(const Elf& elf, std::string* error, std::vector<std::string>* log) {
    try {
        Menu_PortClearTables();
        MipsMachine image;
        image.load(elf);
        Strings strings(image);
        import_credits(elf, strings, log);
        NisListCount = import_list(elf, strings, "C_NIS_Handler__FUcP9M_CONTROLUiUiii", NisListSource, kNisListMax);
        LevelListCount =
            import_list(elf, strings, "C_LBPMMAP_Handler__FUcP9M_CONTROLUiUiii", LevelListSource, kLevelListMax);
        if (log) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "página de depuração: %u cenas em \"Previews\", %u fases na lista",
                          NisListCount, LevelListCount);
            log->push_back(buf);
        }
        return true;
    } catch (const MipsError& e) {
        if (error) *error = std::string("tabelas do menu: ") + e.what();
    } catch (const std::exception& e) {
        if (error) *error = std::string("tabelas do menu: ") + e.what();
    }
    Menu_PortClearTables();
    return false;
}

int check_special_codenames(const Elf& elf, std::string* report) {
    int ok = 0;
    try {
        MipsMachine m;
        setup_constructor_machine(m, elf);
        allow(m, elf, "Menu_SpecialCodenameCheck__FPc");
        uint64_t elf_bonus = 0;
        bool elf_set = false;
        m.hook(function(elf, "Menu_UpgradeCheat__FUlUiUc"), "Menu_UpgradeCheat", [](MipsMachine& mm) {
            mm.set_gpr(MipsMachine::v0, Menu_UpgradeCheat(mm.gpr(MipsMachine::a0), uint32_t(mm.gpr(MipsMachine::a1)),
                                                          uint8_t(mm.gpr(MipsMachine::a2))));
        });
        m.hook(function(elf, "Menu_SetBonus__FUlUcSc"), "Menu_SetBonus", [&](MipsMachine& mm) {
            elf_bonus = mm.gpr(MipsMachine::a0);
            elf_set = true;
        });
        m.hook(function(elf, "strcmp"), "strcmp", [](MipsMachine& mm) {
            const std::string a = c_string(mm, uint32_t(mm.gpr(MipsMachine::a0)));
            const std::string b = c_string(mm, uint32_t(mm.gpr(MipsMachine::a1)));
            const int r = std::strcmp(a.c_str(), b.c_str());
            mm.set_gpr(MipsMachine::v0, uint64_t(int64_t(r < 0 ? -1 : r > 0 ? 1 : 0)));
        });
        const uint32_t bonus_addr = object(elf, "bonus");
        const uint32_t sp_addr = object(elf, "sp_level");
        // O estado do port, guardado e devolvido no fim (Menu_SetBonus também refaz as melhorias de arma).
        uint8_t saved_bonus[sizeof(menu_bonus)];
        std::memcpy(saved_bonus, menu_bonus, sizeof(menu_bonus));
        uint8_t saved_upgrades[sizeof(Upgrades)];
        std::memcpy(saved_upgrades, Upgrades, sizeof(Upgrades));
        uint8_t saved_enabled[12];
        for (uint32_t i = 0; i < 12; ++i) saved_enabled[i] = sp_level[i * 0x18 + 0x10];
        struct State {
            uint64_t bonus;
            uint32_t enabled;
        };
        const State states[3] = {{0, 0x3}, {0x0123456789abcdefull, 0xaaa}, {~0ull, 0xfff}};
        std::vector<std::string> codes;
        for (uint32_t i = 0; Menu_PortSecretCode(i) != nullptr; ++i) codes.push_back(Menu_PortSecretCode(i));
        codes.push_back("NOTACODE");
        codes.push_back("BOND");
        for (const State& st : states) {
            for (const std::string& code : codes) {
                // O ELF.
                std::memcpy(m.mem(bonus_addr, 8), &st.bonus, 8);
                for (uint32_t i = 0; i < 12; ++i) m.mem(sp_addr + i * 0x18 + 0x10, 1)[0] = (st.enabled >> i) & 1;
                std::memset(m.mem(kTextAddr, 0x20), 0, 0x20);
                std::memcpy(m.mem(kTextAddr, 0x20), code.c_str(), code.size());
                elf_set = false;
                elf_bonus = st.bonus;
                m.call(function(elf, "Menu_SpecialCodenameCheck__FPc"), {kTextAddr});
                const uint32_t elf_ret = uint32_t(m.gpr(MipsMachine::v0));
                uint32_t elf_enabled = 0;
                for (uint32_t i = 0; i < 12; ++i) elf_enabled |= uint32_t(m.mem(sp_addr + i * 0x18 + 0x10, 1)[0] & 1) << i;
                // O port.
                std::memcpy(menu_bonus, &st.bonus, 8);
                for (uint32_t i = 0; i < 12; ++i) sp_level[i * 0x18 + 0x10] = uint8_t((st.enabled >> i) & 1);
                char buf[0x20] = {};
                std::memcpy(buf, code.c_str(), code.size());
                const uint32_t port_ret = Menu_SpecialCodenameCheck(buf);
                uint32_t port_enabled = 0;
                for (uint32_t i = 0; i < 12; ++i) port_enabled |= uint32_t(sp_level[i * 0x18 + 0x10] & 1) << i;
                const uint64_t port_bonus = Menu_GetBonus(0);
                if (elf_ret == port_ret && elf_enabled == port_enabled && elf_bonus == port_bonus &&
                    elf_set == (port_ret != 0)) {
                    ++ok;
                } else if (report != nullptr) {
                    char line[200];
                    std::snprintf(line, sizeof(line), " %s(%llx): elf=%u/%03x/%016llx port=%u/%03x/%016llx", code.c_str(),
                                  static_cast<unsigned long long>(st.bonus), elf_ret, elf_enabled,
                                  static_cast<unsigned long long>(elf_bonus), port_ret, port_enabled,
                                  static_cast<unsigned long long>(port_bonus));
                    *report += line;
                }
            }
        }
        std::memcpy(menu_bonus, saved_bonus, sizeof(menu_bonus));
        std::memcpy(Upgrades, saved_upgrades, sizeof(Upgrades));
        for (uint32_t i = 0; i < 12; ++i) sp_level[i * 0x18 + 0x10] = saved_enabled[i];
    } catch (const MipsError& e) {
        if (report) *report += std::string(" ERRO ") + e.what();
    } catch (const std::exception& e) {
        if (report) *report += std::string(" ERRO ") + e.what();
    }
    return ok;
}

}  // namespace nf
