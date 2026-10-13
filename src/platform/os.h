#pragma once
// O sistema operacional atrás de uma interface (só do port; docs/port.md, "Plataforma: interfaces e backends").
//
// O código do jogo, a plataforma psi*, os menus e o laço do nightfire.exe não chamam o sistema direto: pedem aqui os
// caminhos (o executável, a pasta atual, a pasta de dados do usuário), os arquivos (abrir, listar, apagar, ler e
// escrever por faixa os arquivos grandes do disco), os leitores de CD/DVD, o relógio de alta resolução e a espera do
// limite de quadros, a memória virtual (o heap do jogo; a arena da versão de 64 bits), o console e a caixa de erro (a
// linha de comando chega pelo ponto de entrada, platform/entry.h). Este cabeçalho não traz nada do sistema: os tipos são os do C++ (os identificadores de arquivo são
// void*), e cada plataforma tem o seu backend (os_win32.cpp: Win32, a mesma API de sempre; o do macOS é a etapa 3).
//
// Caminhos: std::wstring, como em todo o port (no Windows, UTF-16; no macOS o backend converte de/para UTF-8 nas
// chamadas do sistema). kPathSep é o separador do sistema: os caminhos montados pelo port usam ele (no Windows, '\\'
// como antes: os relatórios e as mensagens não mudam).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace nf::os {

// ---------------------------------------------------------------- texto

// UTF-16 (Windows) ou UTF-32 (macOS) de/para UTF-8.
std::string to_utf8(const std::wstring& w);
std::wstring from_utf8(const std::string& s);

// ---------------------------------------------------------------- caminhos

#if defined(_WIN32)
constexpr wchar_t kPathSep = L'\\';
#else
constexpr wchar_t kPathSep = L'/';
#endif

// dir + o separador + rel, com os '/' de rel trocados pelo separador do sistema ("design/tokens.json" vira
// "design\tokens.json" no Windows): os caminhos que o port monta saem iguais aos de antes. As duas formas pedem a
// memória como as contas que substituíram (dir + L"\\nome" com um nome fixo, dir + L"\\" + nome com um nome montado),
// para que os contadores da arena da versão de 64 bits (a linha "arena" do relatório) não mudem.
inline void fix_separators(std::wstring& path, size_t from) {
    for (size_t i = from; i < path.size(); ++i) {
        if (path[i] == L'/') path[i] = kPathSep;
    }
}
inline std::wstring join(const std::wstring& dir, const wchar_t* rel) {
    const size_t n = std::char_traits<wchar_t>::length(rel);
    std::wstring r;
    r.reserve(dir.size() + 1 + n);  // uma alocação do tamanho da de dir + L"\\nome"
    r += dir;
    r += kPathSep;
    r.append(rel, n);
    fix_separators(r, dir.size() + 1);
    return r;
}
inline std::wstring join(const std::wstring& dir, const std::wstring& rel) {
    std::wstring r = dir + kPathSep + rel;
    fix_separators(r, dir.size() + 1);
    return r;
}

// O executável em uso (o caminho completo).
std::wstring exe_path();
// A pasta atual do processo.
std::wstring current_dir();
// O caminho absoluto (sem resolver links); o próprio caminho se o sistema recusar.
std::wstring full_path(const std::wstring& path);
// A pasta de dados locais do usuário (%LOCALAPPDATA% no Windows; vazia se o sistema não der).
std::wstring local_data_dir();
// Uma variável de ambiente (false: não existe).
bool get_env(const wchar_t* name, std::wstring* value);

// ---------------------------------------------------------------- arquivos

// Existe (arquivo ou pasta).
bool exists(const std::wstring& path);
// Existe e é uma pasta.
bool is_dir(const std::wstring& path);
struct FileInfo {
    uint64_t size = 0;
    uint64_t write_time = 0;  // a hora da última escrita, nas unidades do sistema (só para comparar)
    bool dir = false;
};
bool file_info(const std::wstring& path, FileInfo* out);
// Cria uma pasta (a de cima tem de existir); false se não criou (também se já existe).
bool make_dir(const std::wstring& path);
bool remove_file(const std::wstring& path);
bool remove_dir(const std::wstring& path);  // vazia
// Troca `to` por `from` (renomeia por cima, com a escrita no disco antes de voltar).
bool replace_file(const std::wstring& from, const std::wstring& to);
// O espaço livre no volume da pasta (false se o sistema não disser).
bool free_space(const std::wstring& dir, uint64_t* bytes);
// Os dois caminhos são o mesmo nome (sem diferenciar maiúsculas, como o NTFS e o APFS).
bool same_path(const std::wstring& a, const std::wstring& b);

struct DirEntry {
    std::wstring name;
    bool dir = false;
    uint64_t size = 0;
};
// O que há numa pasta (sem "." e ".."), na ordem do sistema.
std::vector<DirEntry> list_dir(const std::wstring& dir);

// Um FILE* do C (o modo como no fopen: "rb", "wb", "w", "ab"...). Nulo se não abriu. No Windows, _wfopen_s: o
// arquivo aberto não é compartilhado, como antes.
FILE* open_file(const std::wstring& path, const char* mode);

// Arquivos grandes lidos e escritos por faixa (as fontes do disco, a cópia pessoal): o identificador do sistema.
using FileHandle = void*;
// Leitura, compartilhado com quem lê, escreve ou apaga; nulo se não abriu.
FileHandle file_open_read(const std::wstring& path);
// Escrita, criado do zero (o anterior é truncado), sem compartilhar; nulo se não criou.
FileHandle file_create(const std::wstring& path);
bool file_size(FileHandle h, uint64_t* size);
// Lê até n bytes a partir de pos (em pedaços; para no primeiro erro ou no fim). Devolve quantos leu.
size_t file_read_at(FileHandle h, uint64_t pos, void* dst, size_t n);
// Escreve no cursor (em pedaços; para no primeiro erro). Devolve quantos escreveu.
size_t file_write(FileHandle h, const void* src, size_t n);
bool file_flush(FileHandle h);
bool file_close(FileHandle h);

// Enquanto vive, o sistema não mostra as caixas "Não há disco na unidade" (uma unidade de DVD vazia).
class QuietDriveErrors {
public:
    QuietDriveErrors();
    ~QuietDriveErrors();
    QuietDriveErrors(const QuietDriveErrors&) = delete;
    QuietDriveErrors& operator=(const QuietDriveErrors&) = delete;

private:
    unsigned old_ = 0;
};

// ---------------------------------------------------------------- leitores de CD/DVD

// As raízes dos leitores de CD/DVD ("D:\", "E:\"...; no macOS, os volumes ópticos montados).
std::vector<std::wstring> optical_drives();
// A raiz ("D:\") é de um leitor de CD/DVD.
bool is_optical_drive(const std::wstring& root);

// ---------------------------------------------------------------- teclado

// O caractere que a tecla (platform/keys.h) dá no layout do teclado do usuário (0: nenhum). Os nomes das teclas de
// pontuação na tabela de teclas.
uint32_t key_char(int key);
// A tecla (platform/keys.h) está segura agora, pelo estado do teclado do sistema, com ou sem uma janela com o foco (o
// Shift na partida traz a tela de entrada de volta: ui/modern_setup.h). O backend nulo diz que não.
bool key_held(int key);

// ---------------------------------------------------------------- relógio

// O relógio de alta resolução (tiques desde um ponto qualquer) e quantos tiques por segundo.
uint64_t perf_counter();
uint64_t perf_frequency();
// Espera até o prazo, em segundos do std::chrono::steady_clock (o relógio do limite de quadros): o temporizador de alta
// resolução do sistema até ~1 ms antes, depois a CPU cedida em volta (o Sleep comum arredonda para 15,6 ms).
void wait_until(double deadline);

// ---------------------------------------------------------------- memória

// Páginas reservadas e comprometidas, zeradas (alinhadas à granularidade do sistema: 64 KB no Windows); nulo se faltar.
void* vm_alloc(size_t bytes);
void vm_free(void* p);

// A arena da versão de 64 bits (platform/arena.h): reservar sem comprometer, comprometer e devolver pedaços, proteger
// a página de guarda da pilha do jogo.
// O fim da imagem do executável (o começo dela é kP32Base, platform/ptr32.h).
uintptr_t image_end();
// A maior região livre de [from, to), alinhada a align: false se nenhuma.
bool vm_largest_free(uintptr_t from, uintptr_t to, uintptr_t align, uintptr_t* base, uintptr_t* size);
bool vm_reserve_at(uintptr_t base, size_t size);
bool vm_commit(uintptr_t base, size_t size);
void vm_decommit(uintptr_t base, size_t size);
// Sem acesso (a página de guarda) ou de volta a leitura e escrita.
void vm_protect(void* p, size_t size, bool accessible);

// Uma trava leve (sem custo de criação, zerada vale): a da arena, que serve o operator new antes de tudo.
class Mutex {
public:
    constexpr Mutex() = default;  // iniciada na carga do programa (antes de qualquer construtor)
    void lock();
    void unlock();

private:
    void* state_ = nullptr;  // o SRWLOCK do Windows (um ponteiro, zerado = livre)
};

// ---------------------------------------------------------------- processo

// Um texto na saída padrão (um programa de janela: com a saída redirecionada, o identificador já existe; aberto de um
// console, ele se liga ao console do pai). Os bytes vão como estão.
void console_write(const std::string& text);
// O mesmo com texto largo: no console, com os acentos certos; redirecionado, em UTF-8.
void console_write_w(const std::wstring& text);
// Liga stdout e stderr ao console do pai quando o programa de janela não tem saída (o nf_driving).
void attach_parent_console();
// A caixa de erro do sistema (título e texto em UTF-16) e a mesma com texto de 8 bits na página de código do sistema
// (as mensagens das exceções, como antes).
void show_error(const std::wstring& title, const std::wstring& text);
void show_error_narrow(const char* title, const char* text);
// Termina o processo na hora, com o código.
[[noreturn]] void exit_process(int code);
// A memória privada do processo em bytes (o --check do nf_driving).
uint64_t private_memory();

}  // namespace nf::os
