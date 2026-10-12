#pragma once
// Os arquivos BIGF da EA (.VIV, .MUS, .SPE da pasta DRIVING/ do disco) e a compressão RefPack (docs/driving.md, seção 5).
//
// Leitor só do port (o jogo usa o sistema de arquivos assíncrono do REAL: `locatbig` acha a entrada, `hlafile` lê e
// `unpackz`/`REF_decode` descomprime; nada disso foi portado, porque no PC ler o arquivo inteiro de uma vez basta).
// O formato foi conferido com tools/driving_data.py em todos os 23 arquivos do disco:
//  - cabeçalho: "BIGF" (ou "BIG4"), u32 LE o tamanho do arquivo (no MISC.VIV o campo não bate e ninguém o usa),
//    u32 BE o número de entradas, u32 BE o tamanho do cabeçalho;
//  - entradas {u32 BE posição, u32 BE tamanho, nome ASCII terminado em zero} (`data\car\model\vanquish.crp`);
//  - RefPack: 0x10 0xFB (ou com os bits 0x80 = tamanho em 32 bits e 0x01 = tamanho comprimido também presente) e o
//    tamanho descomprimido em 24 bits big-endian, depois comandos de 1 a 4 bytes (literais e cópias para trás).

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/disc_source.h"

namespace drv {

// RefPack: true se o bloco começa com o cabeçalho do RefPack.
bool refpack_is(const uint8_t* data, size_t size);
// Descomprime; false se o fluxo estiver corrompido (cópia antes do começo, fim antes do tamanho do cabeçalho).
bool refpack_decode(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

struct BigfEntry {
    uint32_t offset = 0;
    uint32_t size = 0;
    std::string name;  // com '\' como no arquivo
};

class Bigf {
public:
    Bigf() = default;
    ~Bigf();
    Bigf(const Bigf&) = delete;
    Bigf& operator=(const Bigf&) = delete;

    // Abre e lê o diretório. false se o arquivo não existe ou não é BIGF. Um arquivo do PC, ou um do disco do jogador
    // (platform/disc_source.h: a pasta, a imagem ISO, o leitor; "DRIVING/MISC.VIV").
    bool open(const std::string& path);
    bool open(const nf::DiscSource& disc, const std::string& path);
    // O diretório já lido de um bloco na memória (testes).
    static bool parse_directory(const uint8_t* head, size_t size, std::vector<BigfEntry>& out);

    const std::vector<BigfEntry>& entries() const { return entries_; }
    const std::string& path() const { return path_; }
    // A primeira entrada cujo nome (sem diferenciar maiúsculas, '/' = '\') contém `key`; -1 se nenhuma.
    int find(const std::string& key) const;
    // Os bytes da entrada, já descomprimidos quando são RefPack.
    bool read(int index, std::vector<uint8_t>& out) const;
    // Os bytes como estão no arquivo.
    bool read_raw(int index, std::vector<uint8_t>& out) const;

private:
    bool open_file(nf::DiscFile file, const std::string& path);
    nf::DiscFile f_;
    std::string path_;
    std::vector<BigfEntry> entries_;
};

// O nome da entrada sem o caminho e sem a extensão ("data\car\model\vanquish.crp" -> "vanquish").
std::string bigf_base_name(const std::string& name);

}  // namespace drv
