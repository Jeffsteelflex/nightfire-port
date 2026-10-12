#pragma once
// FILES.BIN: contêiner sem cabeçalho. O índice é o símbolo global FileList do ACTION.ELF,
// um array de entradas de 32 bytes { char name[16]; u32 offset; u32 size; u32 pad[2]; }.
// Os dois arquivos vêm do disco do jogador (platform/disc_source.h: uma pasta, a imagem ISO, o leitor ou a cópia pessoal).

#include <memory>
#include <string>
#include <vector>

#include "core/binary.h"
#include "platform/disc_source.h"

namespace nf {

struct FileEntry {
    std::string name;  // ex.: "07000001.bin"
    uint32_t offset = 0;
    uint32_t size = 0;
};

class FilesBin {
public:
    // O disco do jogador.
    explicit FilesBin(std::shared_ptr<const DiscSource> disc);
    // Um caminho (open_disc: uma pasta com ACTION.ELF e FILES.BIN extraídos, uma imagem .iso, uma unidade).
    explicit FilesBin(const std::wstring& data_dir);

    const std::vector<FileEntry>& entries() const { return entries_; }
    bool has(const std::string& name) const;
    Bytes load(const std::string& name) const;
    const std::shared_ptr<const DiscSource>& disc() const { return disc_; }

private:
    std::shared_ptr<const DiscSource> disc_;
    std::vector<FileEntry> entries_;
};

}  // namespace nf
