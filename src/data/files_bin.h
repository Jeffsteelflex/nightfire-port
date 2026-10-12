#pragma once
// FILES.BIN: contêiner sem cabeçalho. O índice é o símbolo global FileList do ACTION.ELF,
// um array de entradas de 32 bytes { char name[16]; u32 offset; u32 size; u32 pad[2]; }.

#include <string>
#include <vector>

#include "core/binary.h"

namespace nf {

struct FileEntry {
    std::string name;  // ex.: "07000001.bin"
    uint32_t offset = 0;
    uint32_t size = 0;
};

class FilesBin {
public:
    // data_dir: pasta com ACTION.ELF e FILES.BIN extraídos da ISO.
    explicit FilesBin(const std::wstring& data_dir);

    const std::vector<FileEntry>& entries() const { return entries_; }
    bool has(const std::string& name) const;
    Bytes load(const std::string& name) const;

private:
    std::wstring bin_path_;
    std::vector<FileEntry> entries_;
};

}  // namespace nf
