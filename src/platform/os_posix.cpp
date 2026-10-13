// O sistema operacional no macOS e no Linux (POSIX): o backend de platform/os.h (etapa 3 do macOS; docs/port.md,
// "macOS: compilação e CI"). Serve a build headless dessas plataformas (o código neutro e o nf_tests, sem janela) e é a
// base do resto da etapa 3. O que é só de um dos dois fica em #if defined(__APPLE__) / __linux__.
//
// O que já é de verdade: os caminhos (o executável, a pasta atual, a de dados do usuário), os arquivos (stat, open e
// pread, opendir), os volumes ópticos montados, o relógio monotônico e a espera, a memória virtual (mmap, mprotect) com a
// janela da arena de 64 bits (a imagem do executável e as regiões livres depois dela: mach_vm_region no macOS,
// /proc/self/maps no Linux), a trava da arena, o console e o fim do processo. O que pede a janela (a caixa de erro do
// NSAlert, o caractere de uma tecla no layout do usuário, o estado do teclado) vem do os_macos.mm no programa de janela
// do macOS (NF_MACOS_APP); no Linux e na build headless, o stderr, 0 e nenhuma tecla. A pasta dos arquivos do jogador
// (user_files_dir) é ~/Library/Application Support/<app>/ no macOS e a do executável no Linux.
//
// A arena chama image_end, vm_largest_free, vm_reserve_at e vm_commit de dentro do operator new (a primeira alocação do
// programa, antes dos construtores estáticos): essas funções não alocam (nada de std::string, std::vector ou fopen).

#include "platform/os.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <filesystem>

#include <dirent.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <os/lock.h>
#include <sys/mount.h>
#include <sys/param.h>
#elif defined(__linux__)
#include <link.h>
#include <mntent.h>
#endif

#include "platform/crash_report.h"
#include "platform/ptr32.h"

namespace nf::os {

namespace fs = std::filesystem;

// ---------------------------------------------------------------- texto

// wchar_t tem 32 bits no macOS e no Linux (UTF-32); o caminho de UTF-16 fica para o caso de um sistema de 16 bits.
std::string to_utf8(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t c = uint32_t(w[i]);
        if (sizeof(wchar_t) == 2 && c >= 0xd800 && c < 0xdc00 && i + 1 < w.size()) {
            const uint32_t lo = uint32_t(w[i + 1]);
            if (lo >= 0xdc00 && lo < 0xe000) {
                c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
                ++i;
            }
        }
        if (c > 0x10ffff) c = 0xfffd;
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
    w.reserve(s.size());
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

namespace {
std::string narrow(const std::wstring& path) { return to_utf8(path); }
}  // namespace

// ---------------------------------------------------------------- caminhos

std::wstring exe_path() {
#if defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return {};
    char real[PATH_MAX];
    if (realpath(buf, real) != nullptr) return from_utf8(real);
    return from_utf8(buf);
#else
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    return from_utf8(std::string(buf, size_t(n)));
#endif
}

// O pacote .app do executável (o exe_path já vem sem links simbólicos). Só as contas do caminho: no Linux um executável
// nunca fica num lugar assim, e dá vazio.
std::wstring bundle_dir() { return bundle_of_exe(exe_path()); }

std::wstring current_dir() {
    char buf[4096];
    if (getcwd(buf, sizeof(buf)) == nullptr) return {};
    return from_utf8(buf);
}

std::wstring full_path(const std::wstring& path) {
    std::error_code ec;
    const fs::path p = fs::absolute(fs::path(narrow(path)), ec);
    return ec ? path : from_utf8(p.string());
}

std::wstring local_data_dir() {
    // macOS: ~/Library/Application Support; Linux: $XDG_DATA_HOME, senão ~/.local/share (o %LOCALAPPDATA% do Windows).
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == 0) return {};
    return from_utf8(std::string(home) + "/Library/Application Support");
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && *xdg == '/') return from_utf8(xdg);
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == 0) return {};
    return from_utf8(std::string(home) + "/.local/share");
#endif
}

std::wstring user_files_dir(const wchar_t* app) {
#if defined(__APPLE__)
    // ~/Library/Application Support/<app>/ (o executável fica dentro do pacote .app: ali não se grava). A pasta de
    // cima existe em toda conta; a do jogo é criada aqui.
    const std::wstring base = local_data_dir();
    if (base.empty()) return {};
    std::wstring dir = base + L'/' + app;
    mkdir(narrow(dir).c_str(), 0755);
    return dir + L'/';
#else
    // Linux: ao lado do executável, como no Windows.
    (void)app;
    std::wstring dir = exe_path();
    const size_t slash = dir.find_last_of(L'/');
    return slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
#endif
}

bool get_env(const wchar_t* name, std::wstring* value) {
    const char* v = std::getenv(to_utf8(name).c_str());
    if (v == nullptr) return false;
    if (value != nullptr) *value = from_utf8(v);
    return true;
}

// ---------------------------------------------------------------- arquivos

bool exists(const std::wstring& path) {
    struct stat st;
    return stat(narrow(path).c_str(), &st) == 0;
}

bool is_dir(const std::wstring& path) {
    struct stat st;
    return stat(narrow(path).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool file_info(const std::wstring& path, FileInfo* out) {
    struct stat st;
    if (stat(narrow(path).c_str(), &st) != 0) return false;
    out->dir = S_ISDIR(st.st_mode);
    out->size = out->dir ? 0 : uint64_t(st.st_size);
#if defined(__APPLE__)
    out->write_time = uint64_t(st.st_mtimespec.tv_sec) * 1000000000ull + uint64_t(st.st_mtimespec.tv_nsec);
#else
    out->write_time = uint64_t(st.st_mtim.tv_sec) * 1000000000ull + uint64_t(st.st_mtim.tv_nsec);
#endif
    return true;
}

bool make_dir(const std::wstring& path) { return mkdir(narrow(path).c_str(), 0755) == 0; }
bool remove_file(const std::wstring& path) { return unlink(narrow(path).c_str()) == 0; }
bool remove_dir(const std::wstring& path) { return rmdir(narrow(path).c_str()) == 0; }

bool replace_file(const std::wstring& from, const std::wstring& to) {
    // A escrita no disco antes de trocar (o MOVEFILE_WRITE_THROUGH do Windows).
    const std::string f = narrow(from);
    const int fd = open(f.c_str(), O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
    return rename(f.c_str(), narrow(to).c_str()) == 0;
}

bool free_space(const std::wstring& dir, uint64_t* bytes) {
    struct statvfs sv;
    if (statvfs(dir.empty() ? "." : narrow(dir).c_str(), &sv) != 0) return false;
    *bytes = uint64_t(sv.f_bavail) * uint64_t(sv.f_frsize);
    return true;
}

// Sem diferenciar maiúsculas, como no Windows (o APFS também não diferencia por padrão).
bool same_path(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(wint_t(a[i])) != std::towlower(wint_t(b[i]))) return false;
    }
    return true;
}

std::vector<DirEntry> list_dir(const std::wstring& dir) {
    std::vector<DirEntry> out;
    const std::string d = narrow(dir);
    DIR* h = opendir(d.c_str());
    if (h == nullptr) return out;
    while (const dirent* e = readdir(h)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        DirEntry entry;
        entry.name = from_utf8(e->d_name);
        struct stat st;
        const std::string full = d + "/" + e->d_name;
        if (stat(full.c_str(), &st) == 0) {
            entry.dir = S_ISDIR(st.st_mode);
            entry.size = entry.dir ? 0 : uint64_t(st.st_size);
        }
        out.push_back(std::move(entry));
    }
    closedir(h);
    return out;
}

FILE* open_file(const std::wstring& path, const char* mode) { return std::fopen(narrow(path).c_str(), mode); }

// Os identificadores de arquivo são o descritor + 1 (0 fica sendo o nulo).
namespace {
int fd_of(FileHandle h) { return int(reinterpret_cast<intptr_t>(h)) - 1; }
FileHandle handle_of(int fd) { return fd < 0 ? nullptr : reinterpret_cast<FileHandle>(intptr_t(fd) + 1); }
}  // namespace

FileHandle file_open_read(const std::wstring& path) { return handle_of(open(narrow(path).c_str(), O_RDONLY)); }

FileHandle file_create(const std::wstring& path) {
    return handle_of(open(narrow(path).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644));
}

bool file_size(FileHandle h, uint64_t* size) {
    struct stat st;
    if (h == nullptr || fstat(fd_of(h), &st) != 0) return false;
    *size = uint64_t(st.st_size);
    return true;
}

size_t file_read_at(FileHandle h, uint64_t pos, void* dst, size_t n) {
    if (h == nullptr) return 0;
    auto* out = static_cast<uint8_t*>(dst);
    size_t done = 0;
    while (done < n) {
        const size_t chunk = std::min<size_t>(n - done, size_t(1) << 26);
        const ssize_t got = pread(fd_of(h), out + done, chunk, off_t(pos + done));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        done += size_t(got);
    }
    return done;
}

size_t file_write(FileHandle h, const void* src, size_t n) {
    if (h == nullptr) return 0;
    const auto* b = static_cast<const uint8_t*>(src);
    size_t done = 0;
    while (done < n) {
        const size_t chunk = std::min<size_t>(n - done, size_t(1) << 26);
        const ssize_t put = write(fd_of(h), b + done, chunk);
        if (put < 0 && errno == EINTR) continue;
        if (put <= 0) break;
        done += size_t(put);
    }
    return done;
}

bool file_flush(FileHandle h) { return h != nullptr && fsync(fd_of(h)) == 0; }
bool file_close(FileHandle h) { return h != nullptr && close(fd_of(h)) == 0; }

// Fora do Windows o sistema não abre caixas por uma unidade vazia.
QuietDriveErrors::QuietDriveErrors() = default;
QuietDriveErrors::~QuietDriveErrors() = default;

// ---------------------------------------------------------------- leitores de CD/DVD

// Os volumes montados com o sistema de arquivos de um CD/DVD (ISO 9660 ou UDF), como raízes com a barra no fim. No
// macOS também os volumes locais de /Volumes com um disco de PS2 na raiz (SYSTEM.CNF): uma imagem montada pelo Finder ou
// pelo Utilitário de Disco (.iso, .cdr, .dmg) e um disco externo com os arquivos extraídos aparecem como o leitor do
// Windows. Só a tabela de montagem (getmntinfo com MNT_NOWAIT, sem tocar nos volumes) e só os locais: um volume de rede
// que caiu travaria o stat do SYSTEM.CNF.
std::vector<std::wstring> optical_drives() {
    std::vector<std::wstring> out;
#if defined(__APPLE__)
    struct statfs* mounts = nullptr;
    const int n = getmntinfo(&mounts, MNT_NOWAIT);
    std::vector<std::wstring> local;
    for (int i = 0; i < n; ++i) {
        std::wstring root = from_utf8(mounts[i].f_mntonname);
        if (root.empty() || root.back() != L'/') root += L'/';
        if (std::strcmp(mounts[i].f_fstypename, "cd9660") == 0 || std::strcmp(mounts[i].f_fstypename, "udf") == 0) {
            out.push_back(root);
        } else if ((mounts[i].f_flags & MNT_LOCAL) != 0 && std::strncmp(mounts[i].f_mntonname, "/Volumes/", 9) == 0) {
            local.push_back(root);
        }
    }
    for (const std::wstring& root : roots_with(local, L"SYSTEM.CNF")) {
        if (std::find(out.begin(), out.end(), root) == out.end()) out.push_back(root);
    }
#elif defined(__linux__)
    FILE* f = setmntent("/proc/mounts", "r");
    if (f == nullptr) return out;
    while (const mntent* m = getmntent(f)) {
        if (std::strcmp(m->mnt_type, "iso9660") == 0 || std::strcmp(m->mnt_type, "udf") == 0) {
            std::wstring root = from_utf8(m->mnt_dir);
            if (root.empty() || root.back() != L'/') root += L'/';
            out.push_back(root);
        }
    }
    endmntent(f);
#endif
    return out;
}

bool is_optical_drive(const std::wstring& root) {
    std::wstring r = root;
    if (r.empty() || r.back() != L'/') r += L'/';
    for (const std::wstring& d : optical_drives()) {
        if (d == r) return true;
    }
    return false;
}

// ---------------------------------------------------------------- teclado

// No programa de janela do macOS (NF_MACOS_APP: a build sem NF_HEADLESS) o caractere de uma tecla no layout do usuário
// (UCKeyTranslate) e o estado do teclado do sistema vêm de os_macos.mm, com a caixa de erro do NSAlert. Aqui (o Linux e
// a build headless) nenhum caractere, como no backend nulo (a tabela de teclas usa os nomes dela), e nenhuma tecla segura
// (o Shift da partida não traz a tela de entrada).
#if !defined(NF_MACOS_APP)
uint32_t key_char(int) { return 0; }

bool key_held(int) { return false; }
#endif

// ---------------------------------------------------------------- relógio

uint64_t perf_counter() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

uint64_t perf_frequency() { return 1000000000ull; }

namespace {
double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

// Como no Windows: dorme até ~1 ms antes do prazo (o nanosleep é preciso a algumas dezenas de microssegundos aqui),
// depois cede a CPU até o prazo.
void wait_until(double deadline) {
    for (;;) {
        const double left = deadline - now_seconds();
        if (left <= 0.0) return;
        if (left > 0.0015) {
            const double s = left - 0.001;
            timespec ts;
            ts.tv_sec = time_t(s);
            ts.tv_nsec = long((s - double(ts.tv_sec)) * 1e9);
            nanosleep(&ts, nullptr);
            continue;
        }
        sched_yield();
    }
}

// ---------------------------------------------------------------- memória

namespace {
// As regiões de vm_alloc e os tamanhos delas (o munmap pede o tamanho; o VirtualFree do Windows não).
struct VmBlock {
    void* p;
    size_t size;
};
VmBlock g_vm_blocks[32];
Mutex g_vm_lock;
}  // namespace

void* vm_alloc(size_t bytes) {
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return nullptr;
    g_vm_lock.lock();
    for (VmBlock& b : g_vm_blocks) {
        if (b.p == nullptr) {
            b = {p, bytes};
            g_vm_lock.unlock();
            return p;  // páginas novas do mmap já vêm zeradas
        }
    }
    g_vm_lock.unlock();
    munmap(p, bytes);
    return nullptr;
}

void vm_free(void* p) {
    if (p == nullptr) return;
    g_vm_lock.lock();
    for (VmBlock& b : g_vm_blocks) {
        if (b.p == p) {
            munmap(p, b.size);
            b = {nullptr, 0};
            break;
        }
    }
    g_vm_lock.unlock();
}

uintptr_t image_end() {
    // O fim do último segmento do executável (o fim do .bss no Linux, o do __LINKEDIT no macOS).
    uintptr_t end = 0;
#if defined(__APPLE__)
    // O cabeçalho Mach-O pela base sem origem conhecida (o símbolo é declarado como char: ptr32.h).
    const auto* header = reinterpret_cast<const mach_header_64*>(nf::p32_window_base());
    const auto* cmd = reinterpret_cast<const load_command*>(header + 1);
    uintptr_t text_vmaddr = 0;
    uintptr_t max_end = 0;
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        if (cmd->cmd == LC_SEGMENT_64) {
            const auto* seg = reinterpret_cast<const segment_command_64*>(cmd);
            if (std::strcmp(seg->segname, "__PAGEZERO") != 0) {
                if (std::strcmp(seg->segname, "__TEXT") == 0) text_vmaddr = uintptr_t(seg->vmaddr);
                const uintptr_t e = uintptr_t(seg->vmaddr + seg->vmsize);
                if (e > max_end) max_end = e;
            }
        }
        cmd = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(cmd) + cmd->cmdsize);
    }
    const uintptr_t slide = reinterpret_cast<uintptr_t>(header) - text_vmaddr;
    end = max_end + slide;
#else
    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t, void* data) -> int {
            // O primeiro objeto é o executável.
            uintptr_t top = 0;
            for (int i = 0; i < info->dlpi_phnum; ++i) {
                const auto& ph = info->dlpi_phdr[i];
                if (ph.p_type != PT_LOAD) continue;
                const uintptr_t e = uintptr_t(info->dlpi_addr + ph.p_vaddr + ph.p_memsz);
                if (e > top) top = e;
            }
            *static_cast<uintptr_t*>(data) = top;
            return 1;
        },
        &end);
#endif
    const uintptr_t page = uintptr_t(sysconf(_SC_PAGESIZE));
    return (end + page - 1) & ~(page - 1);
}

namespace {
// Chama fn(começo, fim) para cada região mapeada do processo, em ordem crescente, sem alocar.
template <class F>
void for_each_mapping(uintptr_t from, F&& fn) {
#if defined(__APPLE__)
    mach_vm_address_t addr = mach_vm_address_t(from);
    for (;;) {
        mach_vm_size_t size = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        const kern_return_t kr = mach_vm_region(mach_task_self(), &addr, &size, VM_REGION_BASIC_INFO_64,
                                                reinterpret_cast<vm_region_info_t>(&info), &count, &object);
        if (kr != KERN_SUCCESS) break;
        if (!fn(uintptr_t(addr), uintptr_t(addr + size))) break;
        addr += size;
    }
#else
    // /proc/self/maps, linha a linha ("começo-fim perms ..."), com um buffer fixo.
    (void)from;
    const int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0) return;
    static char buf[16384];
    char line[512];
    size_t len = 0;
    bool stop = false;
    for (;;) {
        const ssize_t got = read(fd, buf, sizeof(buf));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        for (ssize_t i = 0; i < got && !stop; ++i) {
            if (buf[i] != '\n') {
                if (len + 1 < sizeof(line)) line[len++] = buf[i];
                continue;
            }
            line[len] = 0;
            len = 0;
            char* dash = nullptr;
            const uintptr_t lo = uintptr_t(std::strtoull(line, &dash, 16));
            if (dash == nullptr || *dash != '-') continue;
            const uintptr_t hi = uintptr_t(std::strtoull(dash + 1, nullptr, 16));
            if (!fn(lo, hi)) stop = true;
        }
        if (stop) break;
    }
    close(fd);
#endif
}
}  // namespace

bool vm_largest_free(uintptr_t from, uintptr_t to, uintptr_t align, uintptr_t* base, uintptr_t* size) {
    uintptr_t best = 0, best_size = 0;
    auto gap = [&](uintptr_t lo, uintptr_t hi) {
        if (lo < from) lo = from;
        if (hi > to) hi = to;
        lo = (lo + align - 1) & ~(align - 1);
        hi &= ~(align - 1);
        if (hi > lo && hi - lo > best_size) {
            best = lo;
            best_size = hi - lo;
        }
    };
    uintptr_t cursor = from;
    for_each_mapping(from, [&](uintptr_t lo, uintptr_t hi) {
        if (lo >= to) return false;
        if (hi <= cursor) return true;
        if (lo > cursor) gap(cursor, lo);
        cursor = hi;
        return true;
    });
    if (cursor < to) gap(cursor, to);
    *base = best;
    *size = best_size;
    return best_size != 0;
}

bool vm_reserve_at(uintptr_t base, size_t size) {
    int flags = MAP_PRIVATE | MAP_ANON;
#if defined(MAP_NORESERVE)
    flags |= MAP_NORESERVE;
#endif
#if defined(MAP_FIXED_NOREPLACE)
    flags |= MAP_FIXED_NOREPLACE;  // Linux: no endereço ou nada (sem ele, o endereço é só uma sugestão)
#endif
    void* r = mmap(reinterpret_cast<void*>(base), size, PROT_NONE, flags, -1, 0);
    if (r == MAP_FAILED) return false;
    if (r != reinterpret_cast<void*>(base)) {
        munmap(r, size);
        return false;
    }
    return true;
}

bool vm_commit(uintptr_t base, size_t size) {
    return mprotect(reinterpret_cast<void*>(base), size, PROT_READ | PROT_WRITE) == 0;
}

void vm_decommit(uintptr_t base, size_t size) {
    // Páginas novas no lugar (o conteúdo vai embora, como o MEM_DECOMMIT), de volta a só reservadas.
    int flags = MAP_PRIVATE | MAP_ANON | MAP_FIXED;
#if defined(MAP_NORESERVE)
    flags |= MAP_NORESERVE;
#endif
    mmap(reinterpret_cast<void*>(base), size, PROT_NONE, flags, -1, 0);
}

void vm_protect(void* p, size_t size, bool accessible) {
    mprotect(p, size, accessible ? PROT_READ | PROT_WRITE : PROT_NONE);
}

// A trava: zerada vale (constexpr, antes de qualquer construtor). No macOS o os_unfair_lock (OS_UNFAIR_LOCK_INIT é 0);
// no Linux uma trava de giro que cede a CPU (a da arena quase nunca é disputada).
#if defined(__APPLE__)
static_assert(sizeof(os_unfair_lock) <= sizeof(void*), "o os_unfair_lock cabe no estado da Mutex");
void Mutex::lock() { os_unfair_lock_lock(reinterpret_cast<os_unfair_lock*>(&state_)); }
void Mutex::unlock() { os_unfair_lock_unlock(reinterpret_cast<os_unfair_lock*>(&state_)); }
#else
void Mutex::lock() {
    while (__atomic_exchange_n(&state_, reinterpret_cast<void*>(1), __ATOMIC_ACQUIRE) != nullptr) sched_yield();
}
void Mutex::unlock() { __atomic_store_n(&state_, nullptr, __ATOMIC_RELEASE); }
#endif

// ---------------------------------------------------------------- processo

void console_write(const std::string& text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

void console_write_w(const std::wstring& text) { console_write(to_utf8(text)); }

// Um programa de console aqui sempre tem a saída do terminal que o abriu.
void attach_parent_console() {}

// A caixa de erro: no programa de janela do macOS o NSAlert (os_macos.mm); aqui o stderr.
#if !defined(NF_MACOS_APP)
void show_error(const std::wstring& title, const std::wstring& text) {
    std::fprintf(stderr, "%s: %s\n", to_utf8(title).c_str(), to_utf8(text).c_str());
}

void show_error_narrow(const char* title, const char* text) { std::fprintf(stderr, "%s: %s\n", title, text); }
#endif

void exit_process(int code) {
    std::fflush(nullptr);
    // O registro do programa de janela do macOS recebe o que ainda está no caminho (platform/crash_report.h).
    nf::crash_report_flush();
    _exit(code);
}

uint64_t private_memory() {
#if defined(__APPLE__)
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return 0;
    }
    return uint64_t(info.phys_footprint);
#else
    // A memória anônima residente (RssAnon de /proc/self/status), o mais perto do PrivateUsage do Windows.
    FILE* f = std::fopen("/proc/self/status", "r");
    if (f == nullptr) return 0;
    char line[256];
    uint64_t kb = 0;
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        if (std::strncmp(line, "RssAnon:", 8) == 0) {
            kb = std::strtoull(line + 8, nullptr, 10);
            break;
        }
    }
    std::fclose(f);
    return kb << 10;
#endif
}

}  // namespace nf::os
