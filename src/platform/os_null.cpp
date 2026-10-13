// O sistema operacional sem o sistema: o backend nulo de platform/os.h, só com o C++ padrão (std::filesystem,
// std::chrono, o C runtime). Serve a opção NF_HEADLESS do CMake, que compila as bibliotecas neutras e o nf_tests sem
// nenhum backend do Windows (a prova de que a separação fecha) e é o ponto de partida do backend do macOS (etapa 3).
// O que só uma janela ou um sistema de verdade dá (os leitores de DVD, a caixa de erro, o console do pai, a memória
// virtual da arena de 64 bits) aqui não faz nada ou devolve vazio.

#include "platform/os.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <new>
#include <system_error>
#include <thread>

namespace nf::os {

namespace fs = std::filesystem;

// ---------------------------------------------------------------- texto (UTF-8 <-> UTF-16/UTF-32 à mão)

std::string to_utf8(const std::wstring& w) {
    std::string s;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t c = uint32_t(w[i]);
        if (sizeof(wchar_t) == 2 && c >= 0xd800 && c < 0xdc00 && i + 1 < w.size()) {
            const uint32_t lo = uint32_t(w[i + 1]);
            if (lo >= 0xdc00 && lo < 0xe000) {
                c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
                ++i;
            }
        }
        if (c < 0x80) {
            s.push_back(char(c));
        } else if (c < 0x800) {
            s.push_back(char(0xc0 | (c >> 6)));
            s.push_back(char(0x80 | (c & 0x3f)));
        } else if (c < 0x10000) {
            s.push_back(char(0xe0 | (c >> 12)));
            s.push_back(char(0x80 | ((c >> 6) & 0x3f)));
            s.push_back(char(0x80 | (c & 0x3f)));
        } else {
            s.push_back(char(0xf0 | (c >> 18)));
            s.push_back(char(0x80 | ((c >> 12) & 0x3f)));
            s.push_back(char(0x80 | ((c >> 6) & 0x3f)));
            s.push_back(char(0x80 | (c & 0x3f)));
        }
    }
    return s;
}

std::wstring from_utf8(const std::string& s) {
    std::wstring w;
    for (size_t i = 0; i < s.size();) {
        const uint8_t b = uint8_t(s[i]);
        uint32_t c = 0xfffd;
        size_t n = 1;
        if (b < 0x80) {
            c = b;
        } else if ((b & 0xe0) == 0xc0 && i + 1 < s.size()) {
            c = (uint32_t(b & 0x1f) << 6) | (uint8_t(s[i + 1]) & 0x3f);
            n = 2;
        } else if ((b & 0xf0) == 0xe0 && i + 2 < s.size()) {
            c = (uint32_t(b & 0x0f) << 12) | (uint32_t(uint8_t(s[i + 1]) & 0x3f) << 6) | (uint8_t(s[i + 2]) & 0x3f);
            n = 3;
        } else if ((b & 0xf8) == 0xf0 && i + 3 < s.size()) {
            c = (uint32_t(b & 0x07) << 18) | (uint32_t(uint8_t(s[i + 1]) & 0x3f) << 12) |
                (uint32_t(uint8_t(s[i + 2]) & 0x3f) << 6) | (uint8_t(s[i + 3]) & 0x3f);
            n = 4;
        }
        if (sizeof(wchar_t) == 2 && c >= 0x10000) {
            w.push_back(wchar_t(0xd800 + ((c - 0x10000) >> 10)));
            w.push_back(wchar_t(0xdc00 + ((c - 0x10000) & 0x3ff)));
        } else {
            w.push_back(wchar_t(c));
        }
        i += n;
    }
    return w;
}

// ---------------------------------------------------------------- caminhos

std::wstring exe_path() { return {}; }  // sem o sistema: desconhecido (o laço não roda sem um backend de verdade)

std::wstring current_dir() {
    std::error_code ec;
    return fs::current_path(ec).wstring();
}

std::wstring full_path(const std::wstring& path) {
    std::error_code ec;
    const fs::path p = fs::absolute(fs::path(path), ec);
    return ec ? path : p.wstring();
}

std::wstring local_data_dir() {
    std::wstring v;
    if (get_env(L"LOCALAPPDATA", &v)) return v;
    return {};
}

bool get_env(const wchar_t* name, std::wstring* value) {
    const char* v = std::getenv(to_utf8(name).c_str());
    if (v == nullptr) return false;
    if (value != nullptr) *value = from_utf8(v);
    return true;
}

// ---------------------------------------------------------------- arquivos

bool exists(const std::wstring& path) {
    std::error_code ec;
    return fs::exists(fs::path(path), ec);
}

bool is_dir(const std::wstring& path) {
    std::error_code ec;
    return fs::is_directory(fs::path(path), ec);
}

bool file_info(const std::wstring& path, FileInfo* out) {
    std::error_code ec;
    const fs::path p(path);
    const fs::file_status st = fs::status(p, ec);
    if (ec || !fs::exists(st)) return false;
    out->dir = fs::is_directory(st);
    out->size = out->dir ? 0 : uint64_t(fs::file_size(p, ec));
    out->write_time = uint64_t(fs::last_write_time(p, ec).time_since_epoch().count());
    return true;
}

bool make_dir(const std::wstring& path) {
    std::error_code ec;
    return fs::create_directory(fs::path(path), ec);
}

bool remove_file(const std::wstring& path) {
    std::error_code ec;
    return !fs::is_directory(fs::path(path), ec) && fs::remove(fs::path(path), ec);
}

bool remove_dir(const std::wstring& path) {
    std::error_code ec;
    return fs::is_directory(fs::path(path), ec) && fs::remove(fs::path(path), ec);
}

bool replace_file(const std::wstring& from, const std::wstring& to) {
    std::error_code ec;
    fs::rename(fs::path(from), fs::path(to), ec);
    return !ec;
}

bool free_space(const std::wstring& dir, uint64_t* bytes) {
    std::error_code ec;
    const fs::space_info si = fs::space(dir.empty() ? fs::current_path(ec) : fs::path(dir), ec);
    if (ec) return false;
    *bytes = uint64_t(si.available);
    return true;
}

bool same_path(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(wint_t(a[i])) != std::towlower(wint_t(b[i]))) return false;
    }
    return true;
}

std::vector<DirEntry> list_dir(const std::wstring& dir) {
    std::vector<DirEntry> out;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        DirEntry e;
        e.name = it->path().filename().wstring();
        e.dir = it->is_directory(ec);
        e.size = e.dir ? 0 : uint64_t(it->file_size(ec));
        out.push_back(std::move(e));
    }
    return out;
}

FILE* open_file(const std::wstring& path, const char* mode) {
#if defined(_MSC_VER)
    wchar_t wmode[16] = {};
    for (size_t i = 0; i + 1 < 16 && mode[i] != 0; ++i) wmode[i] = wchar_t(uint8_t(mode[i]));
    return _wfopen(path.c_str(), wmode);  // o C runtime (não a API do Windows): caminhos largos
#else
    return std::fopen(to_utf8(path).c_str(), mode);
#endif
}

// Os identificadores de arquivo são FILE* do C.
FileHandle file_open_read(const std::wstring& path) { return open_file(path, "rb"); }
FileHandle file_create(const std::wstring& path) { return open_file(path, "wb"); }

namespace {
bool seek(FILE* f, uint64_t pos) {
#if defined(_MSC_VER)
    return _fseeki64(f, int64_t(pos), SEEK_SET) == 0;
#else
    return fseeko(f, off_t(pos), SEEK_SET) == 0;
#endif
}
}  // namespace

bool file_size(FileHandle h, uint64_t* size) {
    auto* f = static_cast<FILE*>(h);
    if (f == nullptr) return false;
#if defined(_MSC_VER)
    if (_fseeki64(f, 0, SEEK_END) != 0) return false;
    const int64_t n = _ftelli64(f);
#else
    if (fseeko(f, 0, SEEK_END) != 0) return false;
    const int64_t n = int64_t(ftello(f));
#endif
    if (n < 0) return false;
    *size = uint64_t(n);
    return true;
}

size_t file_read_at(FileHandle h, uint64_t pos, void* dst, size_t n) {
    auto* f = static_cast<FILE*>(h);
    if (f == nullptr || !seek(f, pos)) return 0;
    return std::fread(dst, 1, n, f);
}

size_t file_write(FileHandle h, const void* src, size_t n) {
    auto* f = static_cast<FILE*>(h);
    return f == nullptr ? 0 : std::fwrite(src, 1, n, f);
}

bool file_flush(FileHandle h) { return h != nullptr && std::fflush(static_cast<FILE*>(h)) == 0; }
bool file_close(FileHandle h) { return h != nullptr && std::fclose(static_cast<FILE*>(h)) == 0; }

QuietDriveErrors::QuietDriveErrors() = default;
QuietDriveErrors::~QuietDriveErrors() = default;

// ---------------------------------------------------------------- leitores de CD/DVD, teclado

std::vector<std::wstring> optical_drives() { return {}; }
bool is_optical_drive(const std::wstring&) { return false; }
uint32_t key_char(int) { return 0; }
bool key_held(int) { return false; }

// ---------------------------------------------------------------- relógio

uint64_t perf_counter() { return uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()); }

uint64_t perf_frequency() {
    return uint64_t(std::chrono::steady_clock::period::den / std::chrono::steady_clock::period::num);
}

void wait_until(double deadline) {
    const auto when = std::chrono::steady_clock::time_point(
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(deadline)));
    std::this_thread::sleep_until(when);
}

// ---------------------------------------------------------------- memória

namespace {
constexpr std::align_val_t kPageAlign{size_t(64) << 10};  // a granularidade do VirtualAlloc
}  // namespace

void* vm_alloc(size_t bytes) {
    void* p = ::operator new(bytes, kPageAlign, std::nothrow);
    if (p != nullptr) std::memset(p, 0, bytes);
    return p;
}

void vm_free(void* p) {
    if (p != nullptr) ::operator delete(p, kPageAlign);
}

// A arena só existe na versão de 64 bits, que precisa de um backend de verdade (a janela de 4 GB perto da imagem).
uintptr_t image_end() { return 0; }
bool vm_largest_free(uintptr_t, uintptr_t, uintptr_t, uintptr_t* base, uintptr_t* size) {
    *base = *size = 0;
    return false;
}
bool vm_reserve_at(uintptr_t, size_t) { return false; }
bool vm_commit(uintptr_t, size_t) { return false; }
void vm_decommit(uintptr_t, size_t) {}
void vm_protect(void*, size_t, bool) {}

// Sem threads da arena no nulo (a arena é só de 64 bits): a trava não faz nada.
void Mutex::lock() {}
void Mutex::unlock() {}

// ---------------------------------------------------------------- processo

void console_write(const std::string& text) { std::fwrite(text.data(), 1, text.size(), stdout); }
void console_write_w(const std::wstring& text) { console_write(to_utf8(text)); }
void attach_parent_console() {}
void show_error(const std::wstring& title, const std::wstring& text) {
    std::fprintf(stderr, "%s: %s\n", to_utf8(title).c_str(), to_utf8(text).c_str());
}
void show_error_narrow(const char* title, const char* text) { std::fprintf(stderr, "%s: %s\n", title, text); }
void exit_process(int code) { std::_Exit(code); }
uint64_t private_memory() { return 0; }

}  // namespace nf::os
