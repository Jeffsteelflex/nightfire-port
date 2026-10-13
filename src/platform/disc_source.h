#pragma once
// O disco do jogo no PC (só do port; docs/port.md, "Instalação fácil"): de onde vêm os arquivos do disco do jogador
// (ACTION.ELF, SYSTEM.CNF, FILES.BIN, PS2\, MOVIES\, DRIVING\...). Toda leitura deles passa por aqui: o ELF na partida
// (game/boot.h), o FILES.BIN (platform/psi_file.h, data/files_bin.h), os bancos de som e a música (platform/sfx_iop.h),
// os filmes (platform/psi_movie.h) e os arquivos do DRIVING\ (src/driving/bigf.h). Quatro fontes, a mesma interface:
//
//  - uma pasta com os arquivos extraídos do disco (o jeito de antes: a raiz do repositório, ou ao lado do executável);
//  - uma imagem do disco (.iso): o ISO 9660 do DVD do PS2 (os arquivos são faixas contínuas da imagem; os nomes com
//    ";1", sem diferenciar maiúsculas; os registros de diretório que passam de um setor). O UDF que vem junto nos DVDs
//    não é lido: o ISO 9660 basta;
//  - o disco num leitor de DVD: o Windows o monta como uma letra (X:\), e ele é lido como uma pasta;
//  - a cópia pessoal (platform/disc_pack.h): os arquivos depois da imagem PE do próprio executável.
//
// Os caminhos são os do disco, com '/' ("PS2/ENGLISH/SB_0/SB_0.SFX", "MOVIES/30_FPS/07370048.PSS"), sem diferenciar
// maiúsculas, sem a versão ";1".
//
// A versão do disco vem do SYSTEM.CNF (a linha BOOT2 = cdrom0:\SLUS_205.79;1 dá "SLUS-20579"); o jogo diz qual espera
// (DiscTitle: game/boot.h tem o do Nightfire americano) e check_disc recusa os outros com o motivo. Uma pasta sem
// SYSTEM.CNF (só ACTION.ELF e FILES.BIN extraídos, como antes) é aceita sem a versão conferida.
//
// A procura (find_disc), quando a linha de comando não diz: a cópia pessoal (o próprio executável), a pasta do
// executável (os arquivos extraídos, ou uma imagem .iso nela), a pasta atual e as de cima do executável (o repositório,
// para quem compila), o [game] disc do nightfire.ini e os leitores de CD/DVD. Sem nada, o main mostra a tela de
// configuração (src/ui/modern_setup.h).
//
// Nada aqui é do Nightfire: o jogo dá o DiscTitle (a "007 Anthology" reaproveita).

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nf {

// Um arquivo do disco aberto para leitura: uma faixa [base, base + size) de um arquivo do PC (o próprio arquivo numa
// pasta, um pedaço da imagem ISO ou da cópia pessoal). Cada um tem o seu identificador do sistema (os::FileHandle,
// platform/os.h; abrir o mesmo arquivo duas vezes dá dois cursores independentes). Só se move, não se copia.
class DiscFile {
public:
    DiscFile() = default;
    ~DiscFile();
    DiscFile(DiscFile&& o) noexcept;
    DiscFile& operator=(DiscFile&& o) noexcept;
    DiscFile(const DiscFile&) = delete;
    DiscFile& operator=(const DiscFile&) = delete;

    // Um arquivo do PC inteiro (size = UINT64_MAX: até o fim) ou uma faixa dele. Fechado se não abriu.
    static DiscFile open_host(const std::wstring& path, uint64_t base = 0, uint64_t size = UINT64_MAX);

    bool is_open() const { return handle_ != nullptr; }
    void close();
    uint64_t size() const { return size_; }
    uint64_t tell() const { return pos_; }
    // O cursor (fora do arquivo: fica no fim).
    void seek(uint64_t pos) { pos_ = pos < size_ ? pos : size_; }
    // Lê a partir do cursor e o avança; devolve quantos bytes leu (menos que n no fim ou num erro).
    size_t read(void* dst, size_t n);
    // Lê a partir de pos sem mexer no cursor.
    size_t read_at(uint64_t pos, void* dst, size_t n) const;
    // O arquivo inteiro.
    bool read_all(std::vector<uint8_t>& out);

private:
    void* handle_ = nullptr;  // os::FileHandle
    uint64_t base_ = 0;
    uint64_t size_ = 0;
    uint64_t pos_ = 0;
};

enum class DiscKind : uint8_t {
    Folder,  // uma pasta com os arquivos extraídos
    Image,   // uma imagem .iso
    Drive,   // um leitor de CD/DVD (uma pasta na raiz de uma letra)
    Packed,  // a cópia pessoal (disc_pack.h)
};
const char* disc_kind_name(DiscKind k);  // "pasta", "imagem", "unidade", "copia"

struct DiscEntry {
    std::string path;     // como no disco, com '/' ("PS2/ENGLISH/SB_0/SB_0.SFX")
    uint64_t size = 0;
    uint64_t offset = 0;  // as fontes de índice (a imagem, a cópia): onde começa no arquivo do PC
};

// O caminho de um arquivo do disco na forma usada para comparar: '/' como separador, sem '/' no começo, maiúsculas,
// sem a versão (";1") nem o ponto final de um nome sem extensão ("README." -> "README").
std::string disc_normalize(std::string_view path);

class DiscSource {
public:
    virtual ~DiscSource() = default;
    DiscKind kind() const { return kind_; }
    // A pasta, a imagem, a raiz da unidade ("D:\") ou o executável.
    const std::wstring& location() const { return location_; }
    // A pasta do PC onde o disco está: a própria pasta, a da imagem ou da cópia, a raiz da unidade. É ali que o port
    // procura primeiro o que é do jogador e fica ao lado dos dados (o pacote de texturas HD).
    std::wstring folder() const;

    virtual DiscFile open(std::string_view path) const = 0;
    virtual bool exists(std::string_view path) const = 0;
    // Os arquivos sob um diretório do disco (vazio: todos), recursivo, em ordem de caminho.
    virtual std::vector<DiscEntry> list(std::string_view dir = {}) const = 0;
    // O arquivo inteiro (false se não existe ou não deu para ler).
    bool read(std::string_view path, std::vector<uint8_t>& out) const;
    std::string describe() const;  // "imagem C:\...\x.iso"

protected:
    DiscSource(DiscKind kind, std::wstring location) : kind_(kind), location_(std::move(location)) {}

private:
    DiscKind kind_;
    std::wstring location_;
};

// Uma fonte com um índice: cada arquivo é uma faixa de um arquivo do PC (a imagem ISO, a cópia pessoal). Os testes
// montam uma à mão.
class IndexedDisc : public DiscSource {
public:
    IndexedDisc(DiscKind kind, std::wstring location, std::wstring host, std::vector<DiscEntry> entries);
    DiscFile open(std::string_view path) const override;
    bool exists(std::string_view path) const override;
    std::vector<DiscEntry> list(std::string_view dir = {}) const override;
    const DiscEntry* find(std::string_view path) const;
    const std::wstring& host() const { return host_; }

private:
    std::wstring host_;              // o arquivo do PC com os bytes
    std::vector<DiscEntry> entries_;  // por disc_normalize(path)
    std::vector<std::string> keys_;
};

// ---------------------------------------------------------------- as fontes

// Uma pasta (também a raiz de uma unidade: kind Drive). Nula se a pasta não existe.
std::shared_ptr<DiscSource> open_disc_folder(const std::wstring& dir, DiscKind kind = DiscKind::Folder);
// Uma imagem ISO 9660. Nula com o motivo em error se não é uma imagem que dê para ler.
std::shared_ptr<DiscSource> open_disc_image(const std::wstring& path, std::string* error = nullptr);
// O que o caminho for: uma pasta ou a raiz de uma unidade de CD/DVD, uma imagem (.iso ou qualquer arquivo com o ISO
// 9660), ou um executável com a cópia pessoal (disc_pack.h). Nula com o motivo em error.
std::shared_ptr<DiscSource> open_disc(const std::wstring& path, std::string* error = nullptr);

// O ISO 9660 de uma imagem lida por read(offset, destino, bytes) -> bytes lidos: os arquivos (caminho, faixa). false com
// o motivo em error (sem o descritor primário, um arquivo intercalado ou fragmentado, um arquivo fora da imagem).
using DiscReader = std::function<size_t(uint64_t offset, void* dst, size_t n)>;
bool parse_iso9660(const DiscReader& read, uint64_t image_size, std::vector<DiscEntry>* out, std::string* error);

// As unidades de CD/DVD do PC ("D:\", "E:\"...), sem as caixas "insira um disco" do Windows.
std::vector<std::wstring> optical_drives();

// ---------------------------------------------------------------- a versão

// O que um jogo espera do disco (o do Nightfire americano: game/boot.h).
struct DiscTitle {
    std::string boot_id;                // "SLUS-20579"
    std::string name;                   // "007: Nightfire (PS2, USA)"
    std::vector<std::string> required;  // os arquivos sem os quais o jogo não roda ("ACTION.ELF", "FILES.BIN")
    // A cópia pessoal: o que entra (arquivos e diretórios terminados em '/') e o que é opcional (os filmes).
    std::vector<std::string> pack;
    std::vector<std::string> pack_optional;
};

enum class DiscStatus : uint8_t {
    Ok,
    Missing,       // o caminho não existe
    Unreadable,    // não é uma pasta nem uma imagem que dê para ler
    NotPs2,        // sem SYSTEM.CNF com BOOT2: não é um disco de PS2
    WrongGame,     // outro jogo (outro BOOT2, sem os arquivos do jogo)
    WrongVersion,  // o mesmo jogo noutra versão (os arquivos do jogo, outro BOOT2)
    Incomplete,    // a versão certa, faltando arquivos
};
const char* disc_status_name(DiscStatus s);

struct DiscCheck {
    DiscStatus status = DiscStatus::Missing;
    std::string boot_id;     // o do SYSTEM.CNF ("SLES-51258"); vazio sem ele
    std::string missing;     // Incomplete: o primeiro arquivo que falta
    bool unverified = false;  // Ok numa pasta sem SYSTEM.CNF (a versão não foi conferida)
    std::string detail;      // o motivo técnico (Unreadable)
    bool ok() const { return status == DiscStatus::Ok; }
};

// O BOOT2 de um SYSTEM.CNF na forma do código do disco ("BOOT2 = cdrom0:\SLUS_205.79;1" -> "SLUS-20579"); vazio sem
// a linha.
std::string disc_boot_id(std::string_view system_cnf);
DiscCheck check_disc(const DiscSource& src, const DiscTitle& title);

// ---------------------------------------------------------------- a procura

enum class DiscOrigin : uint8_t {
    Command,   // --disc / --data
    Packed,    // a cópia pessoal (o próprio executável)
    ExeFolder, // a pasta do executável (os arquivos, ou uma imagem .iso nela)
    WorkFolder,  // a pasta atual
    RepoFolder,  // as pastas de cima do executável (build\Release -> a raiz do repositório)
    Settings,  // [game] disc do nightfire.ini
    Drive,     // um leitor de CD/DVD
    Picked,    // escolhido na tela de configuração
};
const char* disc_origin_name(DiscOrigin o);

// Os lugares da procura (o main preenche; os testes montam à mão).
struct DiscPlaces {
    std::wstring exe;                     // o executável em uso (a cópia pessoal)
    std::wstring exe_dir;
    std::vector<std::wstring> exe_isos;   // as imagens .iso na pasta do executável
    std::wstring cwd;
    std::vector<std::wstring> parents;    // as pastas de cima do executável (até 3)
    std::wstring settings;                // [game] disc
    std::vector<std::wstring> drives;     // optical_drives()
};
// A pasta do executável, as imagens nela, a pasta atual, as de cima e os leitores (settings fica vazio).
DiscPlaces disc_places_here();

struct DiscCandidate {
    std::wstring path;
    DiscOrigin origin;
};
// Os candidatos na ordem da procura (sem repetir um caminho).
std::vector<DiscCandidate> disc_candidates(const DiscPlaces& places);

struct DiscFound {
    std::shared_ptr<DiscSource> source;  // nula: nada serviu
    DiscOrigin origin = DiscOrigin::Command;
    DiscCheck check;
    // Os candidatos que existiam e foram recusados (outra versão, um disco de outro jogo no leitor...): a tela de
    // configuração os mostra.
    struct Rejected {
        std::wstring path;
        DiscOrigin origin;
        DiscCheck check;
    };
    std::vector<Rejected> rejected;
    bool ok() const { return source != nullptr && check.ok(); }
};
// Abre um candidato (open_disc; a cópia pessoal pelo disc_pack.h). Os testes trocam.
using DiscOpener = std::function<std::shared_ptr<DiscSource>(const DiscCandidate&, std::string* error)>;
DiscFound find_disc(const DiscPlaces& places, const DiscTitle& title, const DiscOpener& opener = {});
// Um caminho só (a linha de comando, a escolha da tela): abre e confere.
DiscFound probe_disc(const std::wstring& path, DiscOrigin origin, const DiscTitle& title);

}  // namespace nf
