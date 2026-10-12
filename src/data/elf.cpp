#include "data/elf.h"

namespace nf {

namespace {
constexpr uint32_t kShtProgbits = 1;
constexpr uint32_t kShtSymtab = 2;
}  // namespace

Elf::Elf(Bytes data) : data_(std::move(data)) {
    ByteView d(data_);
    if (d.size() < 0x34 || u32(d, 0) != 0x464C457F) throw FormatError("não é um ELF");
    const uint32_t phoff = u32(d, 0x1C);
    const uint32_t shoff = u32(d, 0x20);
    const uint16_t phentsize = u16(d, 0x2A);
    const uint16_t phnum = u16(d, 0x2C);
    const uint16_t shentsize = u16(d, 0x2E);
    const uint16_t shnum = u16(d, 0x30);
    const uint16_t shstrndx = u16(d, 0x32);
    for (uint32_t i = 0; i < phnum; ++i) {
        const size_t o = phoff + size_t(i) * phentsize;
        segments_.push_back({u32(d, o), u32(d, o + 4), u32(d, o + 8), u32(d, o + 16), u32(d, o + 20)});
    }
    std::vector<uint32_t> names;  // índices no .shstrtab, resolvidos depois de ler todas as seções
    for (uint32_t i = 0; i < shnum; ++i) {
        const size_t o = shoff + size_t(i) * shentsize;
        ElfSection s;
        s.type = u32(d, o + 4);
        s.flags = u32(d, o + 8);
        s.addr = u32(d, o + 12);
        s.offset = u32(d, o + 16);
        s.size = u32(d, o + 20);
        s.link = u32(d, o + 24);
        names.push_back(u32(d, o));
        sections_.push_back(std::move(s));
    }
    if (shstrndx < sections_.size()) {
        for (size_t i = 0; i < sections_.size(); ++i) {
            sections_[i].name = cstring(d, sections_[shstrndx].offset + names[i]);
        }
    }
    for (const ElfSection& s : sections_) {
        if (s.type != kShtSymtab || s.link >= sections_.size()) continue;
        const ElfSection& strtab = sections_[s.link];
        for (uint32_t k = 0; k < s.size / 16; ++k) {
            const size_t o = s.offset + size_t(k) * 16;
            ElfSymbol sym;
            const uint32_t name = u32(d, o);
            sym.value = u32(d, o + 4);
            sym.size = u32(d, o + 8);
            sym.type = u8(d, o + 12) & 0xF;
            sym.section = u16(d, o + 14);
            if (name) sym.name = cstring(d, strtab.offset + name);
            symbols_.push_back(std::move(sym));
        }
    }
}

std::optional<ElfSymbol> Elf::find(const std::string& name) const {
    for (const ElfSymbol& s : symbols_) {
        if (s.name == name) return s;
    }
    return std::nullopt;
}

const ElfSymbol* Elf::symbol_at(uint32_t addr, uint8_t type) const {
    for (const ElfSymbol& s : symbols_) {
        if (s.type != type || s.name.empty()) continue;
        if (addr == s.value || (addr > s.value && addr - s.value < s.size)) return &s;
    }
    return nullptr;
}

ByteView Elf::at(uint32_t vaddr, uint32_t size) const {
    for (const ElfSection& s : sections_) {
        if (s.type == kShtProgbits && s.addr && vaddr >= s.addr && vaddr + size <= s.addr + s.size) {
            return sub(ByteView(data_), s.offset + (vaddr - s.addr), size);
        }
    }
    throw FormatError("endereço fora das seções carregadas");
}

}  // namespace nf
