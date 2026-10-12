#include "game/hud_tables.h"

#include <cstdio>
#include <cstring>
#include <map>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "engine/hud.h"
#include "engine/mission.h"

namespace nf {

namespace {

constexpr uint8_t kFunc = 2;

std::string hex8(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08x", v);
    return buf;
}

class HudImporter {
public:
    HudImporter(const Elf& elf, const MipsMachine& m, CodeResolver resolve) : elf_(elf), m_(m), resolve_(resolve) {}

    uint32_t word(uint32_t addr) const {
        uint32_t v;
        std::memcpy(&v, m_.mem(addr, 4), 4);
        return v;
    }

    uint32_t symbol(const char* name, uint32_t expected_size = 0) {
        const auto s = elf_.find(name);
        if (!s) throw std::runtime_error(std::string("símbolo ") + name + " ausente no ELF");
        if (expected_size != 0 && s->size != expected_size) {
            throw std::runtime_error(std::string("símbolo ") + name + " tem " + std::to_string(s->size) +
                                     " bytes no ELF; o port espera " + std::to_string(expected_size));
        }
        return s->value;
    }

    void copy(const char* name, void* dst, uint32_t size) {
        const uint32_t addr = symbol(name, size);
        std::memcpy(dst, m_.mem(addr, size), size);
    }

    // Ponteiro de código: a função portada pelo nome do símbolo (nunca o endereço do PS2).
    uint32_t code(uint32_t addr, const std::string& where) {
        if (addr == 0) return 0;
        const ElfSymbol* fn = elf_.symbol_at(addr, kFunc);
        if (fn == nullptr || fn->value != addr) throw std::runtime_error(where + " = " + hex8(addr) + " não é uma função");
        const uint32_t port = resolve_ != nullptr ? resolve_(fn->name.c_str(), addr) : 0;
        if (port == 0) throw std::runtime_error(where + ": " + fn->name + " não portada");
        ++functions_;
        return port;
    }

    // Formato de fonte (string curta do .sdata).
    char* format(uint32_t addr) {
        if (addr == 0) return nullptr;
        if (const auto it = formats_.find(addr); it != formats_.end()) return it->second;
        if (formats_.size() >= kHudFormatPool) throw std::runtime_error("formatos de fonte demais no HUD");
        char* dst = HudFormatPool[formats_.size()];
        const uint8_t* src = m_.mem(addr, 16);
        size_t n = 0;
        while (n < 15 && src[n] != 0) ++n;
        if (src[n] != 0) throw std::runtime_error("formato de fonte em " + hex8(addr) + " longo demais");
        std::memcpy(dst, src, n);
        dst[n] = 0;
        formats_[addr] = dst;
        return dst;
    }

    void fix_sprite_info(SpriteInfo* info, uint32_t elf_addr) {
        uint32_t fmt;
        std::memcpy(&fmt, m_.mem(elf_addr + 0x20, 4), 4);
        info->format = format(fmt);
    }

    SpriteInfo* sprites(uint32_t addr, uint32_t count) {
        if (addr == 0 || count == 0) return nullptr;
        if (const auto it = sprinfo_.find(addr); it != sprinfo_.end()) return it->second;
        if (sprinfo_used_ + count > kHudSprInfoPool) throw std::runtime_error("SpriteInfo demais no HUD");
        SpriteInfo* dst = &HudSprInfoPool[sprinfo_used_];
        std::memcpy(dst, m_.mem(addr, count * sizeof(SpriteInfo)), count * sizeof(SpriteInfo));
        for (uint32_t i = 0; i < count; ++i) fix_sprite_info(&dst[i], addr + i * uint32_t(sizeof(SpriteInfo)));
        sprinfo_used_ += count;
        sprinfo_[addr] = dst;
        return dst;
    }

    HUDPANECREATE_tag* pane(uint32_t addr) {
        if (addr == 0) return nullptr;
        if (const auto it = panes_.find(addr); it != panes_.end()) return it->second;
        if (panes_.size() >= kHudPanePool) throw std::runtime_error("panes demais no HUD");
        HUDPANECREATE_tag* dst = &HudPanePool[panes_.size()];
        const uint8_t* src = m_.mem(addr, sizeof(HUDPANECREATE_tag));
        std::memcpy(dst, src, sizeof(HUDPANECREATE_tag));
        const ElfSymbol* sym = elf_.symbol_at(addr, 1);
        const std::string where = sym != nullptr && sym->value == addr ? sym->name : hex8(addr);
        dst->create = reinterpret_cast<HudCreateFn>(uintptr_t(code(word(addr + 8), where + "+0x8")));
        dst->update = reinterpret_cast<HudUpdateFn>(uintptr_t(code(word(addr + 0xc), where + "+0xc")));
        dst->sprites = sprites(word(addr + 0x10), dst->count);
        panes_[addr] = dst;
        return dst;
    }

    void pane_list(const char* name, HUDPANECREATE_tag** dst) {
        const uint32_t list = symbol(name, kHudPanes * 4);
        for (uint32_t i = 0; i < kHudPanes; ++i) dst[i] = pane(word(list + i * 4));
    }

    HUDPANECREATE_tag* known_pane(const char* name) {
        const uint32_t addr = symbol(name);
        const auto it = panes_.find(addr);
        if (it == panes_.end()) throw std::runtime_error(std::string(name) + " não está em PaneList");
        return it->second;
    }

    void missions() {
        const uint32_t table = symbol("MissionData", sizeof(MissionData));
        std::memcpy(MissionData, m_.mem(table, sizeof(MissionData)), sizeof(MissionData));
        std::map<uint32_t, OBJECTIVE_tag*> copied;
        for (uint32_t i = 0; i < kMissionCount; ++i) {
            const uint32_t addr = word(table + i * uint32_t(sizeof(MISSIONDATA_tag)) + 0xc);
            const uint32_t count = MissionData[i].count;
            OBJECTIVE_tag* dst = nullptr;
            if (addr != 0 && count != 0) {
                if (const auto it = copied.find(addr); it != copied.end()) {
                    dst = it->second;
                } else {
                    if (objectives_ + count > kMissionObjectivePool) throw std::runtime_error("objetivos demais na MissionData");
                    dst = &MissionObjectives[objectives_];
                    std::memcpy(dst, m_.mem(addr, count * sizeof(OBJECTIVE_tag)), count * sizeof(OBJECTIVE_tag));
                    objectives_ += count;
                    copied[addr] = dst;
                }
            }
            MissionData[i].objectives = dst;
        }
    }

    std::string summary() const {
        return "HUD: " + std::to_string(panes_.size()) + " panes, " + std::to_string(sprinfo_used_) + " SpriteInfo, " +
               std::to_string(formats_.size()) + " formatos de fonte, " + std::to_string(functions_) +
               " ponteiros de código; missão: " + std::to_string(objectives_) + " objetivos";
    }

private:
    const Elf& elf_;
    const MipsMachine& m_;
    CodeResolver resolve_;
    std::map<uint32_t, HUDPANECREATE_tag*> panes_;
    std::map<uint32_t, SpriteInfo*> sprinfo_;
    std::map<uint32_t, char*> formats_;
    uint32_t sprinfo_used_ = 0;
    uint32_t objectives_ = 0;
    int functions_ = 0;
};

}  // namespace

bool init_hud_tables(const Elf& elf, CodeResolver resolve_code, std::string* error, std::vector<std::string>* log) {
    try {
        MipsMachine m;
        setup_constructor_machine(m, elf);
        // Só o construtor do HUD: ele monta as panes de mensagem no .bss.
        uint32_t ctor = 0;
        for (const ElfSymbol& s : elf.symbols()) {
            if (s.type == kFunc && s.name.rfind("_GLOBAL_$I$HUD_Init", 0) == 0) ctor = s.value;
        }
        if (ctor == 0) throw std::runtime_error("construtor estático do HUD ausente no ELF");
        m.call(ctor);

        HudImporter in(elf, m, resolve_code);
        std::memset(HudPanePool, 0, sizeof(HudPanePool));
        std::memset(HudSprInfoPool, 0, sizeof(HudSprInfoPool));
        std::memset(HudFormatPool, 0, sizeof(HudFormatPool));
        in.pane_list("PaneList", PaneList);
        in.pane_list("MPPaneList", MPPaneList);
        HudMsgMissionStatusPane = in.known_pane("MsgMissionStatusPane");
        HudMsgObjectiveStatusPane = in.known_pane("MsgObjectiveStatusPane");
        in.copy("CrossHair", &CrossHair, sizeof(CrossHair));
        in.fix_sprite_info(&CrossHair, in.symbol("CrossHair"));
        in.copy("HUDCrossCoords", HUDCrossCoords, sizeof(HUDCrossCoords));
        in.copy("BulletImg.189", BulletImg_189, sizeof(BulletImg_189));
        in.copy("TexUV.291", TexUV_291, sizeof(TexUV_291));
        in.missions();
        in.copy("sp_level", sp_level, sizeof(sp_level));
        if (log) log->push_back(in.summary());
        return true;
    } catch (const MipsError& e) {
        if (error) *error = std::string("construtor do HUD: ") + e.what();
    } catch (const std::exception& e) {
        if (error) *error = std::string("tabelas do HUD: ") + e.what();
    }
    return false;
}

}  // namespace nf
