// A cópia pessoal em um arquivo só. Ver disc_pack.h.

#include "platform/disc_pack.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace nf {

namespace {

constexpr char kMagic[16] = {'N', 'F', 'P', 'C', '-', 'D', 'I', 'S', 'C', '-', 'P', 'A', 'C', 'K', '-', '1'};
constexpr uint32_t kFormat = 1;
constexpr size_t kFooter = 64;
constexpr uint64_t kAlignProgram = 4096;
constexpr uint64_t kAlignFile = 2048;

struct Footer {
    uint64_t pe_size = 0;
    uint64_t index_offset = 0;
    uint32_t index_size = 0;
    uint32_t format = 0;
    uint32_t flags = 0;
    uint32_t reserved = 0;
    char boot_id[16] = {};
    char magic[16] = {};
};
static_assert(sizeof(Footer) == kFooter, "o rodapé tem 64 bytes");

std::string narrow_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(std::max(0, n)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring full_path(const std::wstring& p) {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetFullPathNameW(p.c_str(), DWORD(std::size(buf)), buf, nullptr);
    return n > 0 && n < std::size(buf) ? std::wstring(buf, n) : p;
}

std::wstring parent_of(const std::wstring& p) {
    const size_t cut = p.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::wstring() : p.substr(0, cut);
}

bool read_footer(const DiscFile& f, Footer* out) {
    if (!f.is_open() || f.size() < kFooter + 2) return false;
    if (f.read_at(f.size() - kFooter, out, kFooter) != kFooter) return false;
    return std::memcmp(out->magic, kMagic, sizeof(kMagic)) == 0;
}

uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

// Um arquivo de saída com escrita em blocos grandes.
class Out {
public:
    explicit Out(const std::wstring& path) {
        h_ = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h_ == INVALID_HANDLE_VALUE) h_ = nullptr;
    }
    ~Out() { close(); }
    bool ok() const { return h_ != nullptr && good_; }
    uint64_t pos() const { return pos_; }
    void write(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        while (good_ && n > 0) {
            DWORD put = 0;
            const DWORD chunk = DWORD(std::min<size_t>(n, size_t(1) << 26));
            if (h_ == nullptr || !WriteFile(h_, b, chunk, &put, nullptr) || put == 0) {
                good_ = false;
                break;
            }
            b += put;
            n -= put;
            pos_ += put;
        }
    }
    void pad_to(uint64_t at) {
        static const uint8_t zeros[4096] = {};
        while (good_ && pos_ < at) write(zeros, size_t(std::min<uint64_t>(sizeof(zeros), at - pos_)));
    }
    bool close() {
        bool r = good_;
        if (h_ != nullptr) {
            r = FlushFileBuffers(h_) && r;
            r = CloseHandle(h_) && r;
        }
        h_ = nullptr;
        return r;
    }

private:
    HANDLE h_ = nullptr;
    bool good_ = true;
    uint64_t pos_ = 0;
};

}  // namespace

// ---------------------------------------------------------------- a leitura

std::shared_ptr<DiscSource> open_packed_disc(const std::wstring& exe, std::string* error) {
    if (error != nullptr) error->clear();
    DiscFile f = DiscFile::open_host(exe);
    Footer ft;
    if (!read_footer(f, &ft)) return nullptr;
    const auto bad = [&](const char* why) -> std::shared_ptr<DiscSource> {
        if (error != nullptr) *error = std::string("cópia pessoal estragada: ") + why;
        return nullptr;
    };
    if (ft.format != kFormat) return bad("formato desconhecido (de outra versão do port?)");
    const uint64_t index_end = ft.index_offset + ft.index_size;
    if (ft.pe_size == 0 || ft.index_offset < ft.pe_size || index_end > f.size() - kFooter || ft.index_size < 8) {
        return bad("rodapé fora do arquivo");
    }
    std::vector<uint8_t> idx(ft.index_size);
    if (f.read_at(ft.index_offset, idx.data(), idx.size()) != idx.size()) return bad("índice ilegível");
    if (std::memcmp(idx.data(), "NFIX", 4) != 0) return bad("índice sem a marca");
    uint32_t count = 0;
    std::memcpy(&count, idx.data() + 4, 4);
    std::vector<DiscEntry> entries;
    size_t p = 8;
    for (uint32_t i = 0; i < count; ++i) {
        if (p + 18 > idx.size()) return bad("índice cortado");
        DiscEntry e;
        uint16_t len = 0;
        std::memcpy(&e.offset, idx.data() + p, 8);
        std::memcpy(&e.size, idx.data() + p + 8, 8);
        std::memcpy(&len, idx.data() + p + 16, 2);
        p += 18;
        if (p + len > idx.size()) return bad("índice cortado");
        e.path.assign(reinterpret_cast<const char*>(idx.data() + p), len);
        p += len;
        if (e.offset < ft.pe_size || e.offset + e.size > ft.index_offset) return bad("arquivo fora da cópia");
        entries.push_back(std::move(e));
    }
    return std::make_shared<IndexedDisc>(DiscKind::Packed, exe, exe, std::move(entries));
}

uint64_t packed_program_size(const std::wstring& exe) {
    DiscFile f = DiscFile::open_host(exe);
    Footer ft;
    if (read_footer(f, &ft) && ft.pe_size > 0 && ft.pe_size <= f.size()) return ft.pe_size;
    return f.size();
}

bool inside_git_tree(const std::wstring& path) {
    std::wstring d = parent_of(full_path(path));
    for (int guard = 0; guard < 64 && !d.empty(); ++guard) {
        if (GetFileAttributesW((d + L"\\.git").c_str()) != INVALID_FILE_ATTRIBUTES) return true;
        const std::wstring up = parent_of(d);
        if (up == d) break;
        d = up;
    }
    return false;
}

std::wstring default_pack_path(const std::wstring& exe) {
    const std::wstring dir = parent_of(exe);
    std::wstring name = exe.substr(dir.size() + (dir.empty() ? 0 : 1));
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) name.resize(dot);
    // Uma cópia feita por uma cópia não fica "-personal-personal".
    const std::wstring tail = L"-personal";
    if (name.size() > tail.size() && name.compare(name.size() - tail.size(), tail.size(), tail) == 0) {
        name.resize(name.size() - tail.size());
    }
    return (dir.empty() ? std::wstring() : dir + L"\\") + name + tail + L".exe";
}

const char* pack_error_name(PackError e) {
    switch (e) {
        case PackError::None: return "nenhum";
        case PackError::InsideGitTree: return "dentro_do_git";
        case PackError::SameFile: return "o_proprio_executavel";
        case PackError::NotVerified: return "sem_system_cnf";
        case PackError::MissingFile: return "falta_arquivo";
        case PackError::NoSpace: return "sem_espaco";
        case PackError::Write: return "escrita";
        case PackError::Read: return "leitura";
        case PackError::Cancelled: return "cancelado";
    }
    return "?";
}

// ---------------------------------------------------------------- a escrita

PackPlan pack_plan(const DiscSource& src, const DiscTitle& title, const std::wstring& exe, bool movies,
                   const std::wstring& extras_dir, const std::string& extras_prefix) {
    PackPlan plan;
    plan.movies = movies;
    plan.program = packed_program_size(exe);
    if (!src.exists("SYSTEM.CNF")) {
        plan.error = PackError::NotVerified;
        plan.detail = "SYSTEM.CNF";
        return plan;
    }
    const auto add = [&](const std::string& item, bool required) {
        if (!item.empty() && item.back() == '/') {
            std::vector<DiscEntry> files = src.list(item.substr(0, item.size() - 1));
            if (files.empty() && required) {
                plan.error = PackError::MissingFile;
                plan.detail = item;
            }
            for (DiscEntry& e : files) plan.files.push_back(std::move(e));
            return;
        }
        DiscFile f = src.open(item);
        if (!f.is_open()) {
            if (required) {
                plan.error = PackError::MissingFile;
                plan.detail = item;
            }
            return;
        }
        plan.files.push_back(DiscEntry{item, f.size(), 0});
    };
    for (const std::string& item : title.pack) add(item, true);
    if (movies) {
        for (const std::string& item : title.pack_optional) add(item, false);
    }
    // Os caminhos do disco como no disco (sem ";1"), um de cada.
    for (DiscEntry& e : plan.files) {
        const size_t semi = e.path.find(';');
        if (semi != std::string::npos) e.path.resize(semi);
    }
    std::sort(plan.files.begin(), plan.files.end(),
              [](const DiscEntry& a, const DiscEntry& b) { return disc_normalize(a.path) < disc_normalize(b.path); });
    plan.files.erase(std::unique(plan.files.begin(), plan.files.end(),
                                 [](const DiscEntry& a, const DiscEntry& b) { return disc_normalize(a.path) == disc_normalize(b.path); }),
                     plan.files.end());
    if (!extras_dir.empty()) {
        std::error_code ec;
        const std::filesystem::path root(extras_dir);
        for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::wstring rel = std::filesystem::relative(it->path(), root, ec).wstring();
            for (wchar_t& c : rel) {
                if (c == L'\\') c = L'/';
            }
            plan.extras.emplace_back(extras_prefix + narrow_utf8(rel), it->path().wstring());
            plan.bytes += uint64_t(it->file_size(ec));
        }
        std::sort(plan.extras.begin(), plan.extras.end());
    }
    plan.bytes += plan.program;
    for (const DiscEntry& e : plan.files) plan.bytes += e.size;
    return plan;
}

bool pack_disc(const DiscSource& src, const DiscTitle& title, const PackPlan& plan, const std::wstring& exe,
               const std::wstring& out, PackProgress* progress, PackError* error, std::string* detail) {
    const auto fail = [&](PackError e, const std::string& d) {
        if (error != nullptr) *error = e;
        if (detail != nullptr) *detail = d;
        return false;
    };
    if (error != nullptr) *error = PackError::None;
    if (plan.error != PackError::None) return fail(plan.error, plan.detail);
    const std::wstring out_full = full_path(out);
    if (inside_git_tree(out_full)) return fail(PackError::InsideGitTree, narrow_utf8(out_full));
    if (_wcsicmp(out_full.c_str(), full_path(exe).c_str()) == 0) return fail(PackError::SameFile, narrow_utf8(out_full));
    // O espaço: o total mais uma margem de 64 MB.
    {
        ULARGE_INTEGER free_bytes{};
        const std::wstring dir = parent_of(out_full);
        if (GetDiskFreeSpaceExW(dir.empty() ? nullptr : (dir + L"\\").c_str(), &free_bytes, nullptr, nullptr) &&
            free_bytes.QuadPart < plan.bytes + (64ull << 20)) {
            return fail(PackError::NoSpace, std::to_string((plan.bytes + (64ull << 20)) >> 20) + " MB");
        }
    }
    DiscFile program = DiscFile::open_host(exe, 0, plan.program);
    if (!program.is_open() || program.size() != plan.program) return fail(PackError::Read, narrow_utf8(exe));
    if (progress != nullptr) {
        progress->total = plan.bytes;
        progress->done = 0;
    }
    const std::wstring part = out_full + L".part";
    struct Removal {
        std::wstring path;
        bool keep = false;
        ~Removal() {
            if (!keep) DeleteFileW(path.c_str());
        }
    } removal{part};
    Out o(part);
    if (!o.ok()) return fail(PackError::Write, narrow_utf8(part));
    std::vector<uint8_t> buf(size_t(4) << 20);
    const auto copy = [&](DiscFile& in, uint64_t size, const std::string& name) -> bool {
        uint64_t left = size;
        while (left > 0) {
            if (progress != nullptr && progress->cancel) {
                fail(PackError::Cancelled, std::string());
                return false;
            }
            const size_t n = size_t(std::min<uint64_t>(left, buf.size()));
            if (in.read(buf.data(), n) != n) {
                fail(PackError::Read, name);
                return false;
            }
            o.write(buf.data(), n);
            if (!o.ok()) {
                fail(PackError::Write, narrow_utf8(part));
                return false;
            }
            left -= n;
            if (progress != nullptr) progress->done += n;
        }
        return true;
    };
    if (!copy(program, plan.program, narrow_utf8(exe))) return false;
    o.pad_to(align_up(o.pos(), kAlignProgram));
    std::vector<DiscEntry> index;
    for (const DiscEntry& e : plan.files) {
        DiscFile in = src.open(e.path);
        if (!in.is_open()) return fail(PackError::Read, e.path);
        o.pad_to(align_up(o.pos(), kAlignFile));
        index.push_back(DiscEntry{e.path, in.size(), o.pos()});
        if (!copy(in, in.size(), e.path)) return false;
    }
    for (const auto& [path, host] : plan.extras) {
        DiscFile in = DiscFile::open_host(host);
        if (!in.is_open()) return fail(PackError::Read, narrow_utf8(host));
        o.pad_to(align_up(o.pos(), kAlignFile));
        index.push_back(DiscEntry{path, in.size(), o.pos()});
        if (!copy(in, in.size(), path)) return false;
    }
    o.pad_to(align_up(o.pos(), kAlignFile));
    // O índice.
    std::vector<uint8_t> idx(8);
    std::memcpy(idx.data(), "NFIX", 4);
    const uint32_t count = uint32_t(index.size());
    std::memcpy(idx.data() + 4, &count, 4);
    for (const DiscEntry& e : index) {
        const size_t at = idx.size();
        const uint16_t len = uint16_t(std::min<size_t>(e.path.size(), 0xffff));
        idx.resize(at + 18 + len);
        std::memcpy(idx.data() + at, &e.offset, 8);
        std::memcpy(idx.data() + at + 8, &e.size, 8);
        std::memcpy(idx.data() + at + 16, &len, 2);
        std::memcpy(idx.data() + at + 18, e.path.data(), len);
    }
    Footer ft;
    ft.pe_size = plan.program;
    ft.index_offset = o.pos();
    ft.index_size = uint32_t(idx.size());
    ft.format = kFormat;
    ft.flags = plan.movies ? 1u : 0u;
    std::snprintf(ft.boot_id, sizeof(ft.boot_id), "%s", title.boot_id.c_str());
    std::memcpy(ft.magic, kMagic, sizeof(kMagic));
    o.write(idx.data(), idx.size());
    o.write(&ft, sizeof(ft));
    if (!o.ok() || !o.close()) return fail(PackError::Write, narrow_utf8(part));
    if (!MoveFileExW(part.c_str(), out_full.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return fail(PackError::Write, narrow_utf8(out_full));
    }
    removal.keep = true;
    if (progress != nullptr) progress->done = progress->total.load();
    return true;
}

std::wstring extract_packed_extras(const DiscSource& packed, const std::string& prefix, const std::wstring& root_dir) {
    const std::vector<DiscEntry> files = packed.list(prefix);
    if (files.empty()) return {};
    std::wstring root = root_dir;
    if (root.empty()) {
        wchar_t* base = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) || base == nullptr) return {};
        root = std::wstring(base) + L"\\Nightfire PC";
        CoTaskMemFree(base);
    }
    // Uma marca da cópia (o tamanho e a data do executável): outra cópia, outra pasta.
    std::wstring stamp;
    {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (GetFileAttributesExW(packed.location().c_str(), GetFileExInfoStandard, &a)) {
            wchar_t s[64];
            std::swprintf(s, 64, L"%08x%08x-%08x", a.nFileSizeHigh, a.nFileSizeLow, a.ftLastWriteTime.dwLowDateTime);
            stamp = s;
        }
    }
    // A pasta tem o nome do último pedaço do prefixo ("PORT/HD_UI" -> "hd_ui-<marca>").
    const std::string sub = disc_normalize(prefix);
    const size_t slash = sub.find_last_of('/');
    std::wstring leaf;
    for (char c : sub.substr(slash == std::string::npos ? 0 : slash + 1)) leaf.push_back(wchar_t(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    const std::wstring dir = root + L"\\" + leaf + L"-" + stamp;
    if (GetFileAttributesW((dir + L"\\.ok").c_str()) != INVALID_FILE_ATTRIBUTES) return dir;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string norm_prefix = disc_normalize(prefix) + "/";
    for (const DiscEntry& e : files) {
        // O caminho original (com as maiúsculas de quem empacotou) depois do prefixo.
        const std::string rel = e.path.substr(std::min(e.path.size(), norm_prefix.size()));
        std::wstring wrel;
        const int n = MultiByteToWideChar(CP_UTF8, 0, rel.data(), int(rel.size()), nullptr, 0);
        wrel.resize(size_t(std::max(0, n)));
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, rel.data(), int(rel.size()), wrel.data(), n);
        for (wchar_t& c : wrel) {
            if (c == L'/') c = L'\\';
        }
        const std::filesystem::path target = std::filesystem::path(dir) / wrel;
        std::filesystem::create_directories(target.parent_path(), ec);
        std::vector<uint8_t> data;
        if (!packed.read(e.path, data)) return {};
        FILE* f = nullptr;
        if (_wfopen_s(&f, target.wstring().c_str(), L"wb") != 0 || f == nullptr) return {};
        const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
        std::fclose(f);
        if (!ok) return {};
    }
    FILE* mark = nullptr;
    if (_wfopen_s(&mark, (dir + L"\\.ok").c_str(), L"wb") == 0 && mark != nullptr) std::fclose(mark);
    return dir;
}

}  // namespace nf
