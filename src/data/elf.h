#pragma once
// Leitor mínimo de ELF MIPS do PS2: seções carregáveis, segmentos de programa e tabela de símbolos.

#include <optional>
#include <string>
#include <vector>

#include "core/binary.h"

namespace nf {

struct ElfSymbol {
    uint32_t value = 0;
    uint32_t size = 0;
    uint8_t type = 0;  // 0 = NOTYPE, 1 = OBJECT, 2 = FUNC
    uint16_t section = 0;
    std::string name;
};

// Cabeçalho de programa (o carregador do PS2 copia os PT_LOAD para a RAM e zera o resto de memsz).
struct ElfSegment {
    uint32_t type = 0;  // 1 = PT_LOAD
    uint32_t offset = 0, vaddr = 0, filesz = 0, memsz = 0;
};

struct ElfSection {
    std::string name;
    uint32_t type = 0, flags = 0, addr = 0, offset = 0, size = 0, link = 0;
};

class Elf {
public:
    explicit Elf(Bytes data);

    std::optional<ElfSymbol> find(const std::string& name) const;
    // Símbolo do tipo pedido que contém addr ([value, value + size)); os de tamanho 0 só valem no
    // endereço exato. Nulo se nenhum contiver.
    const ElfSymbol* symbol_at(uint32_t addr, uint8_t type) const;
    // Bytes no endereço virtual vaddr, dentro de uma seção com conteúdo no arquivo.
    ByteView at(uint32_t vaddr, uint32_t size) const;

    ByteView file() const { return data_; }
    const std::vector<ElfSection>& sections() const { return sections_; }
    const std::vector<ElfSegment>& segments() const { return segments_; }
    const std::vector<ElfSymbol>& symbols() const { return symbols_; }

private:
    Bytes data_;
    std::vector<ElfSection> sections_;
    std::vector<ElfSegment> segments_;
    std::vector<ElfSymbol> symbols_;
};

}  // namespace nf
