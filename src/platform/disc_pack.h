#pragma once
// A cópia pessoal em um arquivo só (só do port; docs/port.md, "Instalação fácil"): o próprio executável com os arquivos
// do disco do jogador depois da imagem PE (o "overlay", que o carregador do Windows não mapeia) e um rodapé que diz onde
// está o índice. Na partida o executável lê o próprio fim (open_packed_disc): com o rodapé, ele é a fonte do disco
// (DiscKind::Packed, platform/disc_source.h) e não precisa de mais nada.
//
// O formato (little-endian):
//   [0, pe_size)      o executável como foi compilado (sem a cópia de antes, se ele mesmo já era uma cópia)
//   [alinhado a 4096) os arquivos, cada um começando num múltiplo de 2048
//   o índice          "NFIX", a contagem, e por arquivo: deslocamento (u64, no arquivo), tamanho (u64), o caminho do
//                     disco (u16 + bytes, "PS2/ENGLISH/SB_0/SB_0.SFX")
//   o rodapé (64 B)   pe_size (u64), o deslocamento e o tamanho do índice, o formato (1), as bandeiras (1: com os
//                     filmes), o código do disco ("SLUS-20579", 16 bytes) e a marca "NFPC-DISC-PACK-1" no fim.
//
// O que entra é o que o jogo diz (DiscTitle::pack e pack_optional; o Nightfire: ACTION.ELF, SYSTEM.CNF, FILES.BIN, PS2\
// e, se pedido, MOVIES\; o DRIVING\ fica de fora até as missões de carro rodarem), mais os arquivos do port que o
// executável procura ao lado dele (extras: a pasta hd_ui, com as fontes e os SVG do estilo Modern), com o caminho
// "PORT/...": uma cópia sozinha numa pasta qualquer os tira para a pasta de dados do usuário (extract_packed_extras).
//
// Segurança: a cópia contém o jogo do usuário e não pode ser compartilhada (a tela de configuração e o --pack-disc
// avisam); ela vai só para onde o usuário mandar (por padrão ao lado do executável) e nunca para dentro de uma árvore de
// trabalho do git (inside_git_tree: o .gitignore ignora *.exe, mas a regra vale para qualquer nome).

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "platform/disc_source.h"

namespace nf {

// A cópia pessoal existe nesta plataforma: só no Windows (os arquivos depois da imagem PE). No macOS o programa é um
// Mach-O assinado (o arm64 sempre, ao menos ad hoc: bytes depois da assinatura a invalidam) dentro de um pacote .app,
// onde os dados iriam para dentro do pacote ou ao lado dele (etapa 4): a tela de entrada não mostra a linha da cópia e o
// --pack-disc recusa (docs/port.md, "macOS: os backends"). Ler uma cópia continua igual em todo lugar (os testes).
bool personal_copy_supported();

// O executável tem a cópia pessoal? Nula sem o rodapé (error vazio) ou com um rodapé estragado (error com o motivo).
std::shared_ptr<DiscSource> open_packed_disc(const std::wstring& exe, std::string* error = nullptr);
// O tamanho da parte do programa (sem a cópia; o arquivo inteiro num executável comum).
uint64_t packed_program_size(const std::wstring& exe);

// O caminho está dentro de uma árvore de trabalho do git (uma pasta acima dele, ou ele, tem .git)?
bool inside_git_tree(const std::wstring& path);
// O arquivo de saída padrão: ao lado do executável, "<nome>-personal.exe".
std::wstring default_pack_path(const std::wstring& exe);

enum class PackError : uint8_t {
    None,
    InsideGitTree,  // a saída cairia dentro de um repositório
    SameFile,       // a saída é o próprio executável
    NotVerified,    // a fonte não tem SYSTEM.CNF (uma pasta só com ACTION.ELF e FILES.BIN)
    MissingFile,    // falta um arquivo que entra (detail)
    NoSpace,        // falta espaço no disco de destino
    Write,          // não deu para escrever (detail)
    Read,           // não deu para ler o disco ou o executável (detail)
    Cancelled,
};
const char* pack_error_name(PackError e);

struct PackPlan {
    std::vector<DiscEntry> files;                                // do disco
    std::vector<std::pair<std::string, std::wstring>> extras;    // do port: caminho "PORT/..." e o arquivo do PC
    uint64_t program = 0;                                        // a parte do programa
    uint64_t bytes = 0;                                          // o total que vai ser escrito (aproximado: sem o alinhamento)
    bool movies = false;
    PackError error = PackError::None;
    std::string detail;
};
// O que vai entrar. extras_dir: uma pasta do port inteira com o prefixo dado ("PORT/HD_UI/"), ou vazia.
PackPlan pack_plan(const DiscSource& src, const DiscTitle& title, const std::wstring& exe, bool movies,
                   const std::wstring& extras_dir = {}, const std::string& extras_prefix = {});

struct PackProgress {
    std::atomic<uint64_t> done{0};
    std::atomic<uint64_t> total{0};
    std::atomic<bool> cancel{false};
};
// Escreve a cópia (num .part ao lado, trocado pelo nome final no fim). false com o erro.
bool pack_disc(const DiscSource& src, const DiscTitle& title, const PackPlan& plan, const std::wstring& exe,
               const std::wstring& out, PackProgress* progress, PackError* error, std::string* detail);

// Uma cópia pessoal sozinha numa pasta qualquer: os arquivos do port que vieram nela (prefixo "PORT/HD_UI/") numa pasta
// do usuário (%LOCALAPPDATA%\Nightfire PC\<prefixo>-<marca da cópia>; root troca a pasta de cima, os testes), uma vez.
// Devolve a pasta (vazia: nada).
std::wstring extract_packed_extras(const DiscSource& packed, const std::string& prefix, const std::wstring& root = {});

}  // namespace nf
