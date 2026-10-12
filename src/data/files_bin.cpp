#include "data/files_bin.h"

#include <algorithm>
#include <cctype>

#include "data/elf.h"

namespace nf {

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
}  // namespace

FilesBin::FilesBin(const std::wstring& data_dir) : bin_path_(data_dir + L"\\FILES.BIN") {
    Elf elf(read_file(data_dir + L"\\ACTION.ELF"));
    const auto sym = elf.find("FileList");
    if (!sym || sym->size % 32) throw FormatError("símbolo FileList ausente ou com tamanho inesperado");
    ByteView table = elf.at(sym->value, sym->size);
    for (uint32_t i = 0; i < sym->size / 32; ++i) {
        FileEntry e;
        e.name = cstring(table, size_t(i) * 32, 16);
        e.offset = u32(table, size_t(i) * 32 + 16);
        e.size = u32(table, size_t(i) * 32 + 20);
        entries_.push_back(std::move(e));
    }
}

bool FilesBin::has(const std::string& name) const {
    const std::string want = lower(name);
    return std::any_of(entries_.begin(), entries_.end(), [&](const FileEntry& e) { return lower(e.name) == want; });
}

Bytes FilesBin::load(const std::string& name) const {
    const std::string want = lower(name);
    for (const FileEntry& e : entries_) {
        if (lower(e.name) == want) return read_file_range(bin_path_, e.offset, e.size);
    }
    throw FormatError("arquivo não encontrado no FILES.BIN: " + name);
}

}  // namespace nf
