// O sistema operacional no Windows (Win32): o backend de platform/os.h. As chamadas são as de antes desta interface
// (cada função diz de onde veio), então o comportamento não muda.

#include "platform/os.h"

#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <iterator>

namespace nf::os {

// ---------------------------------------------------------------- texto

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(std::max(0, n)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring from_utf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// ---------------------------------------------------------------- caminhos

std::wstring exe_path() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    return std::wstring(buf, n);
}

// No Windows o programa não fica num pacote de aplicativo (o hd_ui e o resto ficam ao lado do executável).
std::wstring bundle_dir() { return {}; }

std::wstring current_dir() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetCurrentDirectoryW(DWORD(std::size(buf)), buf);
    return std::wstring(buf, n < std::size(buf) ? n : 0);
}

std::wstring full_path(const std::wstring& path) {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetFullPathNameW(path.c_str(), DWORD(std::size(buf)), buf, nullptr);
    return n > 0 && n < std::size(buf) ? std::wstring(buf, n) : path;
}

std::wstring local_data_dir() {
    wchar_t* base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) || base == nullptr) {
        if (base != nullptr) CoTaskMemFree(base);
        return {};
    }
    std::wstring dir(base);
    CoTaskMemFree(base);
    return dir;
}

// A pasta do executável, com a barra no fim: as mesmas contas (e as mesmas alocações) que settings.cpp e psi_card.cpp
// faziam antes desta função.
std::wstring user_files_dir(const wchar_t*) {
    std::wstring dir = exe_path();
    const size_t slash = dir.find_last_of(L"\\/");
    dir = slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
    return dir;
}

bool get_env(const wchar_t* name, std::wstring* value) {
    const DWORD need = GetEnvironmentVariableW(name, nullptr, 0);
    if (need == 0) return false;
    std::wstring v(size_t(need), L'\0');
    const DWORD n = GetEnvironmentVariableW(name, v.data(), need);
    if (n == 0 || n >= need) return false;
    v.resize(n);
    if (value != nullptr) *value = v;
    return true;
}

// ---------------------------------------------------------------- arquivos

bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool is_dir(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool file_info(const std::wstring& path, FileInfo* out) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return false;
    out->size = uint64_t(a.nFileSizeHigh) << 32 | a.nFileSizeLow;
    out->write_time = uint64_t(a.ftLastWriteTime.dwHighDateTime) << 32 | a.ftLastWriteTime.dwLowDateTime;
    out->dir = (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return true;
}

bool make_dir(const std::wstring& path) { return CreateDirectoryW(path.c_str(), nullptr) != 0; }
bool remove_file(const std::wstring& path) { return DeleteFileW(path.c_str()) != 0; }
bool remove_dir(const std::wstring& path) { return RemoveDirectoryW(path.c_str()) != 0; }

bool replace_file(const std::wstring& from, const std::wstring& to) {
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool free_space(const std::wstring& dir, uint64_t* bytes) {
    ULARGE_INTEGER free_bytes{};
    if (!GetDiskFreeSpaceExW(dir.empty() ? nullptr : (dir + L"\\").c_str(), &free_bytes, nullptr, nullptr)) return false;
    *bytes = free_bytes.QuadPart;
    return true;
}

bool same_path(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

std::vector<DirEntry> list_dir(const std::wstring& dir) {
    std::vector<DirEntry> out;
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        DirEntry e;
        e.name = name;
        e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size = e.dir ? 0 : (uint64_t(fd.nFileSizeHigh) << 32 | fd.nFileSizeLow);
        out.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

FILE* open_file(const std::wstring& path, const char* mode) {
    wchar_t wmode[16] = {};
    for (size_t i = 0; i + 1 < std::size(wmode) && mode[i] != 0; ++i) wmode[i] = wchar_t(uint8_t(mode[i]));
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), wmode) != 0) return nullptr;
    return f;
}

FileHandle file_open_read(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    return h == INVALID_HANDLE_VALUE ? nullptr : h;
}

FileHandle file_create(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return h == INVALID_HANDLE_VALUE ? nullptr : h;
}

bool file_size(FileHandle h, uint64_t* size) {
    LARGE_INTEGER total{};
    if (h == nullptr || !GetFileSizeEx(h, &total)) return false;
    *size = uint64_t(total.QuadPart);
    return true;
}

size_t file_read_at(FileHandle h, uint64_t pos, void* dst, size_t n) {
    size_t done = 0;
    auto* out = static_cast<uint8_t*>(dst);
    while (done < n) {
        const DWORD chunk = DWORD(std::min<size_t>(n - done, size_t(1) << 26));
        const uint64_t at = pos + done;
        OVERLAPPED ov{};
        ov.Offset = DWORD(at & 0xffffffffu);
        ov.OffsetHigh = DWORD(at >> 32);
        DWORD got = 0;
        if (!ReadFile(h, out + done, chunk, &got, &ov) || got == 0) break;
        done += got;
    }
    return done;
}

size_t file_write(FileHandle h, const void* src, size_t n) {
    const auto* b = static_cast<const uint8_t*>(src);
    size_t done = 0;
    while (done < n) {
        DWORD put = 0;
        const DWORD chunk = DWORD(std::min<size_t>(n - done, size_t(1) << 26));
        if (h == nullptr || !WriteFile(h, b + done, chunk, &put, nullptr) || put == 0) break;
        done += put;
    }
    return done;
}

bool file_flush(FileHandle h) { return FlushFileBuffers(h) != 0; }
bool file_close(FileHandle h) { return CloseHandle(h) != 0; }

QuietDriveErrors::QuietDriveErrors() : old_(SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX)) {}
QuietDriveErrors::~QuietDriveErrors() { SetErrorMode(old_); }

// ---------------------------------------------------------------- leitores de CD/DVD

std::vector<std::wstring> optical_drives() {
    std::vector<std::wstring> out;
    wchar_t buf[512];
    const DWORD n = GetLogicalDriveStringsW(DWORD(std::size(buf)), buf);
    if (n == 0 || n > std::size(buf)) return out;
    for (const wchar_t* d = buf; *d != 0; d += std::wcslen(d) + 1) {
        if (GetDriveTypeW(d) == DRIVE_CDROM) out.push_back(d);
    }
    return out;
}

bool is_optical_drive(const std::wstring& root) { return GetDriveTypeW(root.c_str()) == DRIVE_CDROM; }

// ---------------------------------------------------------------- teclado

uint32_t key_char(int key) { return MapVirtualKeyW(UINT(key), MAPVK_VK_TO_CHAR); }

bool key_held(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }

// ---------------------------------------------------------------- relógio

uint64_t perf_counter() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return uint64_t(now.QuadPart);
}

uint64_t perf_frequency() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return uint64_t(f.QuadPart);
}

namespace {
double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

void wait_until(double deadline) {
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    static HANDLE timer = [] {
        HANDLE t = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        return t != nullptr ? t : CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }();
    for (;;) {
        const double left = deadline - now_seconds();
        if (left <= 0.0) return;
        if (left > 0.0015 && timer != nullptr) {
            LARGE_INTEGER due;
            due.QuadPart = -LONGLONG((left - 0.001) * 1e7);  // em unidades de 100 ns, relativo
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, 100);
                continue;
            }
        }
        YieldProcessor();
        if (left > 0.0005) Sleep(0);
    }
}

// ---------------------------------------------------------------- memória

void* vm_alloc(size_t bytes) { return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); }

void vm_free(void* p) {
    if (p != nullptr) VirtualFree(p, 0, MEM_RELEASE);
}

}  // namespace nf::os

extern "C" char __ImageBase;

namespace nf::os {

uintptr_t image_end() {
    const uintptr_t base = reinterpret_cast<uintptr_t>(&__ImageBase);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return base + nt->OptionalHeader.SizeOfImage;
}

bool vm_largest_free(uintptr_t from, uintptr_t to, uintptr_t align, uintptr_t* base, uintptr_t* size) {
    uintptr_t at = from;
    uintptr_t best = 0, best_size = 0;
    while (at < to) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof(mbi)) == 0) break;
        const uintptr_t region = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        uintptr_t region_end = region + mbi.RegionSize;
        if (region_end > to) region_end = to;
        if (mbi.State == MEM_FREE) {
            const uintptr_t lo = (at + align - 1) & ~(align - 1);
            const uintptr_t hi = region_end & ~(align - 1);
            if (hi > lo && hi - lo > best_size) {
                best = lo;
                best_size = hi - lo;
            }
        }
        at = region_end;
    }
    *base = best;
    *size = best_size;
    return best_size != 0;
}

bool vm_reserve_at(uintptr_t base, size_t size) {
    void* r = VirtualAlloc(reinterpret_cast<void*>(base), size, MEM_RESERVE, PAGE_NOACCESS);
    return r == reinterpret_cast<void*>(base);
}

bool vm_commit(uintptr_t base, size_t size) {
    return VirtualAlloc(reinterpret_cast<void*>(base), size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

void vm_decommit(uintptr_t base, size_t size) { VirtualFree(reinterpret_cast<void*>(base), size, MEM_DECOMMIT); }

void vm_protect(void* p, size_t size, bool accessible) {
    DWORD old_protect = 0;
    VirtualProtect(p, size, accessible ? PAGE_READWRITE : PAGE_NOACCESS, &old_protect);
}

static_assert(sizeof(SRWLOCK) == sizeof(void*), "o SRWLOCK cabe no estado da Mutex");

void Mutex::lock() { AcquireSRWLockExclusive(reinterpret_cast<SRWLOCK*>(&state_)); }
void Mutex::unlock() { ReleaseSRWLockExclusive(reinterpret_cast<SRWLOCK*>(&state_)); }

// ---------------------------------------------------------------- processo


namespace {
// A saída padrão; sem ela, o console do pai (CONOUT$).
HANDLE std_output() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            h = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        }
    }
    return h;
}
}  // namespace

void console_write(const std::string& text) {
    HANDLE h = std_output();
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, text.data(), DWORD(text.size()), &written, nullptr);
}

void console_write_w(const std::wstring& text) {
    HANDLE h = std_output();
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0, written = 0;
    if (GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, text.data(), DWORD(text.size()), &written, nullptr);
        return;
    }
    const std::string u = to_utf8(text);
    WriteFile(h, u.data(), DWORD(u.size()), &written, nullptr);
}

void attach_parent_console() {
    if (GetStdHandle(STD_OUTPUT_HANDLE) == nullptr || GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
    }
}

void show_error(const std::wstring& title, const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_ICONERROR);
}

void show_error_narrow(const char* title, const char* text) { MessageBoxA(nullptr, text, title, MB_ICONERROR); }

void exit_process(int code) { ExitProcess(UINT(code)); }

uint64_t private_memory() {
    PROCESS_MEMORY_COUNTERS_EX pm{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof pm);
    return uint64_t(pm.PrivateUsage);
}

}  // namespace nf::os
