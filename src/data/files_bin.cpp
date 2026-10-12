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

std::shared_ptr<const DiscSource> open_or_throw(const std::wstring& data_dir) {
    std::string error;
    std::shared_ptr<const DiscSource> d = open_disc(data_dir, &error);
    if (d == nullptr) throw FormatError("não consegui abrir os dados do jogo: " + error);
    return d;
}
}  // namespace

FilesBin::FilesBin(const std::wstring& data_dir) : FilesBin(open_or_throw(data_dir)) {}

FilesBin::FilesBin(std::shared_ptr<const DiscSource> disc) : disc_(std::move(disc)) {
    Bytes elf_bytes;
    if (disc_ == nullptr || !disc_->read("ACTION.ELF", elf_bytes)) throw FormatError("não consegui abrir ACTION.ELF");
    Elf elf(std::move(elf_bytes));
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
        if (lower(e.name) != want) continue;
        DiscFile f = disc_->open("FILES.BIN");
        if (!f.is_open()) throw FormatError("não consegui abrir FILES.BIN");
        Bytes data(e.size);
        if (f.read_at(e.offset, data.data(), data.size()) != data.size()) {
            throw FormatError("leitura incompleta de FILES.BIN (" + name + ")");
        }
        return data;
    }
    throw FormatError("arquivo não encontrado no FILES.BIN: " + name);
}

}  // namespace nf
