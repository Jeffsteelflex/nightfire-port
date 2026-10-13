// O disco do jogo no PC: as fontes (pasta, imagem ISO 9660, unidade de CD/DVD), a versão e a procura. Ver
// disc_source.h.

#include "platform/disc_source.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <set>

#include "platform/disc_pack.h"
#include "platform/os.h"

namespace nf {

namespace {

std::string narrow_utf8(const std::wstring& w) { return os::to_utf8(w); }

bool host_exists(const std::wstring& p) { return os::exists(p); }
bool host_is_dir(const std::wstring& p) { return os::is_dir(p); }

std::wstring parent_of(const std::wstring& p) {
    const size_t cut = p.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::wstring() : p.substr(0, cut);
}

// "PS2/ENGLISH/X" -> "PS2\ENGLISH\X" dentro da pasta (o separador do sistema, os::kPathSep).
std::wstring host_path(const std::wstring& root, std::string_view path) {
    std::wstring r = root;
    if (!r.empty() && r.back() != L'\\' && r.back() != L'/') r += os::kPathSep;
    std::wstring w = os::from_utf8(std::string(path));
    for (wchar_t& c : w) {
        if (c == L'/') c = os::kPathSep;
    }
    size_t a = 0;
    while (a < w.size() && w[a] == os::kPathSep) ++a;
    return r + w.substr(a);
}

// Sem as caixas "Não há disco na unidade" do sistema enquanto vivo (uma unidade de DVD vazia).
using QuietErrors = os::QuietDriveErrors;

// Uma pasta (ou a raiz de uma unidade).
class FolderDisc : public DiscSource {
public:
    FolderDisc(DiscKind kind, std::wstring dir) : DiscSource(kind, std::move(dir)) {}
    DiscFile open(std::string_view path) const override {
        QuietErrors quiet;
        return DiscFile::open_host(host_path(location(), strip_version(path)));
    }
    bool exists(std::string_view path) const override {
        QuietErrors quiet;
        const std::wstring p = host_path(location(), strip_version(path));
        return os::exists(p) && !os::is_dir(p);
    }
    std::vector<DiscEntry> list(std::string_view dir) const override {
        QuietErrors quiet;
        std::vector<DiscEntry> out;
        const std::filesystem::path root(location());
        const std::filesystem::path start = dir.empty() ? root : std::filesystem::path(host_path(location(), dir));
        std::error_code ec;
        if (!std::filesystem::is_directory(start, ec)) return out;
        for (std::filesystem::recursive_directory_iterator it(start, std::filesystem::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            DiscEntry e;
            std::wstring rel = std::filesystem::relative(it->path(), root, ec).wstring();
            for (wchar_t& c : rel) {
                if (c == L'\\') c = L'/';
            }
            e.path = narrow_utf8(rel);
            e.size = uint64_t(it->file_size(ec));
            out.push_back(std::move(e));
        }
        std::sort(out.begin(), out.end(), [](const DiscEntry& a, const DiscEntry& b) {
            return disc_normalize(a.path) < disc_normalize(b.path);
        });
        return out;
    }

private:
    // Um nome com ";1" (vindo de quem lê o disco do PS2) acha o arquivo extraído sem ele.
    static std::string strip_version(std::string_view path) {
        std::string s(path);
        const size_t semi = s.find(';');
        if (semi != std::string::npos) s.resize(semi);
        return s;
    }
};

// ---- ISO 9660 ----

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

}  // namespace

// ---------------------------------------------------------------- DiscFile

DiscFile::~DiscFile() { close(); }

DiscFile::DiscFile(DiscFile&& o) noexcept : handle_(o.handle_), base_(o.base_), size_(o.size_), pos_(o.pos_) {
    o.handle_ = nullptr;
    o.size_ = o.pos_ = o.base_ = 0;
}

DiscFile& DiscFile::operator=(DiscFile&& o) noexcept {
    if (this != &o) {
        close();
        handle_ = o.handle_;
        base_ = o.base_;
        size_ = o.size_;
        pos_ = o.pos_;
        o.handle_ = nullptr;
        o.size_ = o.pos_ = o.base_ = 0;
    }
    return *this;
}

DiscFile DiscFile::open_host(const std::wstring& path, uint64_t base, uint64_t size) {
    DiscFile f;
    os::FileHandle h = os::file_open_read(path);
    if (h == nullptr) return f;
    uint64_t total = 0;
    if (!os::file_size(h, &total) || total < base) {
        os::file_close(h);
        return f;
    }
    const uint64_t avail = total - base;
    f.handle_ = h;
    f.base_ = base;
    f.size_ = size == UINT64_MAX ? avail : std::min(size, avail);
    f.pos_ = 0;
    return f;
}

void DiscFile::close() {
    if (handle_ != nullptr) os::file_close(handle_);
    handle_ = nullptr;
    base_ = size_ = pos_ = 0;
}

size_t DiscFile::read_at(uint64_t pos, void* dst, size_t n) const {
    if (handle_ == nullptr || pos >= size_ || n == 0) return 0;
    n = size_t(std::min<uint64_t>(n, size_ - pos));
    return os::file_read_at(handle_, base_ + pos, dst, n);
}

size_t DiscFile::read(void* dst, size_t n) {
    const size_t got = read_at(pos_, dst, n);
    pos_ += got;
    return got;
}

bool DiscFile::read_all(std::vector<uint8_t>& out) {
    out.clear();
    if (handle_ == nullptr) return false;
    out.resize(size_t(size_));
    return read_at(0, out.data(), out.size()) == out.size();
}

// ---------------------------------------------------------------- DiscSource

const char* disc_kind_name(DiscKind k) {
    switch (k) {
        case DiscKind::Folder: return "pasta";
        case DiscKind::Image: return "imagem";
        case DiscKind::Drive: return "unidade";
        case DiscKind::Packed: return "copia";
    }
    return "?";
}

std::string disc_normalize(std::string_view path) {
    std::string s;
    s.reserve(path.size());
    for (char c : path) {
        if (c == '\\') c = '/';
        s.push_back(char(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c));
    }
    size_t a = 0;
    while (a < s.size() && s[a] == '/') ++a;
    s.erase(0, a);
    const size_t semi = s.find(';');
    if (semi != std::string::npos) s.resize(semi);
    // Um nome sem extensão no ISO 9660 termina em '.' ("README.;1"); um diretório no fim também sai.
    while (!s.empty() && (s.back() == '.' || s.back() == '/')) s.pop_back();
    return s;
}

std::wstring DiscSource::folder() const {
    switch (kind_) {
        case DiscKind::Folder:
        case DiscKind::Drive: return location_;
        default: return parent_of(location_);
    }
}

bool DiscSource::read(std::string_view path, std::vector<uint8_t>& out) const {
    DiscFile f = open(path);
    if (!f.is_open()) {
        out.clear();
        return false;
    }
    return f.read_all(out);
}

std::string DiscSource::describe() const { return std::string(disc_kind_name(kind_)) + " " + narrow_utf8(location_); }

IndexedDisc::IndexedDisc(DiscKind kind, std::wstring location, std::wstring host, std::vector<DiscEntry> entries)
    : DiscSource(kind, std::move(location)), host_(std::move(host)) {
    std::sort(entries.begin(), entries.end(),
              [](const DiscEntry& a, const DiscEntry& b) { return disc_normalize(a.path) < disc_normalize(b.path); });
    for (DiscEntry& e : entries) {
        keys_.push_back(disc_normalize(e.path));
        entries_.push_back(std::move(e));
    }
}

const DiscEntry* IndexedDisc::find(std::string_view path) const {
    const std::string key = disc_normalize(path);
    const auto it = std::lower_bound(keys_.begin(), keys_.end(), key);
    if (it == keys_.end() || *it != key) return nullptr;
    return &entries_[size_t(it - keys_.begin())];
}

DiscFile IndexedDisc::open(std::string_view path) const {
    const DiscEntry* e = find(path);
    if (e == nullptr) return DiscFile{};
    return DiscFile::open_host(host_, e->offset, e->size);
}

bool IndexedDisc::exists(std::string_view path) const { return find(path) != nullptr; }

std::vector<DiscEntry> IndexedDisc::list(std::string_view dir) const {
    std::string prefix = disc_normalize(dir);
    if (!prefix.empty()) prefix += '/';
    std::vector<DiscEntry> out;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (keys_[i].compare(0, prefix.size(), prefix) == 0) out.push_back(entries_[i]);
    }
    return out;
}

// ---------------------------------------------------------------- ISO 9660

bool parse_iso9660(const DiscReader& read, uint64_t image_size, std::vector<DiscEntry>* out, std::string* error) {
    const auto fail = [&](const char* why) {
        if (error != nullptr) *error = why;
        return false;
    };
    out->clear();
    // Os descritores de volume a partir do setor 16 (de 2048 bytes, o tamanho físico do DVD e do CD de dados).
    uint8_t vd[2048];
    bool found = false;
    for (uint32_t s = 16; s < 16 + 64; ++s) {
        if (read(uint64_t(s) * 2048, vd, sizeof(vd)) != sizeof(vd)) break;
        if (std::memcmp(vd + 1, "CD001", 5) != 0) break;
        if (vd[0] == 1) {
            found = true;
            break;
        }
        if (vd[0] == 255) break;
    }
    if (!found) return fail("sem o descritor primário do ISO 9660 (não é uma imagem de disco de 2048 bytes por setor)");
    const uint32_t block = le16(vd + 128);
    if (block != 2048 && block != 1024 && block != 512) return fail("tamanho de bloco lógico estranho no ISO 9660");
    const uint8_t* root = vd + 156;
    struct Dir {
        uint32_t lba, size;
        std::string path;
        int depth;
    };
    std::vector<Dir> todo{{le32(root + 2), le32(root + 10), std::string(), 0}};
    std::set<uint32_t> seen;
    std::vector<uint8_t> data;
    // Um arquivo em várias partes (a bandeira 0x80 em todas menos a última): só aceito se as partes forem contínuas.
    DiscEntry pending;
    bool has_pending = false;
    while (!todo.empty()) {
        const Dir d = todo.back();
        todo.pop_back();
        if (!seen.insert(d.lba).second) continue;
        if (d.depth > 16 || d.size > (16u << 20)) return fail("diretório do ISO 9660 grande ou fundo demais");
        data.assign(d.size, 0);
        if (read(uint64_t(d.lba) * block, data.data(), data.size()) != data.size()) return fail("diretório fora da imagem");
        size_t off = 0;
        while (off < data.size()) {
            const uint8_t len = data[off];
            if (len == 0) {
                // O resto do setor está vazio: os registros não passam de um setor para o outro.
                off = (off / 2048 + 1) * 2048;
                continue;
            }
            if (len < 34 || off + len > data.size()) return fail("registro de diretório inválido no ISO 9660");
            const uint8_t* r = data.data() + off;
            off += len;
            const uint32_t lba = le32(r + 2);
            const uint32_t size = le32(r + 10);
            const uint8_t flags = r[25];
            const uint8_t name_len = r[32];
            if (33u + name_len > len) return fail("nome de registro inválido no ISO 9660");
            const std::string name(reinterpret_cast<const char*>(r + 33), name_len);
            if (name_len == 1 && (name[0] == 0 || name[0] == 1)) continue;  // "." e ".."
            std::string clean = name;
            const size_t semi = clean.find(';');
            if (semi != std::string::npos) clean.resize(semi);
            if (!clean.empty() && clean.back() == '.') clean.pop_back();
            const std::string path = d.path.empty() ? clean : d.path + "/" + clean;
            if (flags & 0x02) {
                todo.push_back(Dir{lba, size, path, d.depth + 1});
                continue;
            }
            if (r[26] != 0 || r[27] != 0) return fail("arquivo intercalado no ISO 9660 (não suportado)");
            const uint64_t at = uint64_t(lba) * block;
            if (has_pending && pending.path == path) {
                if (at != pending.offset + pending.size) return fail("arquivo em partes não contínuas no ISO 9660");
                pending.size += size;
            } else {
                if (has_pending) out->push_back(pending);
                pending = DiscEntry{path, size, at};
                has_pending = true;
            }
            if ((flags & 0x80) == 0) {
                if (pending.offset + pending.size > image_size) return fail("arquivo além do fim da imagem (imagem cortada?)");
                out->push_back(pending);
                has_pending = false;
            }
        }
    }
    if (has_pending) return fail("arquivo em partes sem a última no ISO 9660");
    if (out->empty()) return fail("ISO 9660 sem arquivos");
    return true;
}

// ---------------------------------------------------------------- as fontes

std::shared_ptr<DiscSource> open_disc_folder(const std::wstring& dir, DiscKind kind) {
    QuietErrors quiet;
    if (dir.empty() || !host_is_dir(dir)) return nullptr;
    std::wstring d = dir;
    // A raiz de uma unidade fica com a barra ("D:\"); as outras pastas sem.
    while (d.size() > 3 && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
    if (d.size() == 2 && d[1] == L':') d += L'\\';
    return std::make_shared<FolderDisc>(kind, d);
}

std::shared_ptr<DiscSource> open_disc_image(const std::wstring& path, std::string* error) {
    DiscFile f = DiscFile::open_host(path);
    if (!f.is_open()) {
        if (error != nullptr) *error = host_exists(path) ? "não consegui abrir o arquivo" : "arquivo não encontrado";
        return nullptr;
    }
    std::vector<DiscEntry> entries;
    const DiscReader reader = [&f](uint64_t off, void* dst, size_t n) { return f.read_at(off, dst, n); };
    if (!parse_iso9660(reader, f.size(), &entries, error)) return nullptr;
    return std::make_shared<IndexedDisc>(DiscKind::Image, path, path, std::move(entries));
}

std::shared_ptr<DiscSource> open_disc(const std::wstring& path, std::string* error) {
    QuietErrors quiet;
    if (path.empty()) {
        if (error != nullptr) *error = "caminho vazio";
        return nullptr;
    }
    std::wstring p = path;
    // "D:" é a unidade.
    if (p.size() == 2 && p[1] == L':') p += L'\\';
    if (host_is_dir(p)) {
        const bool root = p.size() == 3 && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/');
        // Fora do Windows a raiz de um volume é uma pasta qualquer (/Volumes/NIGHTFIRE): os::is_optical_drive diz.
        const bool drive = (root || os::kPathSep == L'/') && os::is_optical_drive(p);
        return open_disc_folder(p, drive ? DiscKind::Drive : DiscKind::Folder);
    }
    if (!host_exists(p)) {
        if (error != nullptr) *error = "arquivo não encontrado";
        return nullptr;
    }
    // Um executável com a cópia pessoal no fim.
    std::string pack_error;
    if (std::shared_ptr<DiscSource> packed = open_packed_disc(p, &pack_error)) return packed;
    if (!pack_error.empty()) {
        if (error != nullptr) *error = pack_error;
        return nullptr;
    }
    return open_disc_image(p, error);
}

std::vector<std::wstring> optical_drives() { return os::optical_drives(); }

bool disc_image_extension(const std::wstring& ext) {
    if (ext == L".iso") return true;
#if !defined(_WIN32)
    // O "DVD/CD master" do Utilitário de Disco do macOS: setores de 2048 bytes com o mesmo ISO 9660 (no Windows a procura
    // continua só com .iso, como antes; open_disc abre qualquer arquivo com o ISO 9660 nos dois).
    if (ext == L".cdr") return true;
#endif
    return false;
}

// ---------------------------------------------------------------- a versão

const char* disc_status_name(DiscStatus s) {
    switch (s) {
        case DiscStatus::Ok: return "ok";
        case DiscStatus::Missing: return "ausente";
        case DiscStatus::Unreadable: return "ilegivel";
        case DiscStatus::NotPs2: return "nao_ps2";
        case DiscStatus::WrongGame: return "outro_jogo";
        case DiscStatus::WrongVersion: return "outra_versao";
        case DiscStatus::Incomplete: return "incompleto";
    }
    return "?";
}

std::string disc_boot_id(std::string_view cnf) {
    size_t pos = 0;
    while (pos < cnf.size()) {
        size_t end = cnf.find('\n', pos);
        if (end == std::string_view::npos) end = cnf.size();
        std::string_view line = cnf.substr(pos, end - pos);
        pos = end + 1;
        size_t a = 0;
        while (a < line.size() && (line[a] == ' ' || line[a] == '\t')) ++a;
        line.remove_prefix(a);
        if (line.size() < 5) continue;
        std::string key(line.substr(0, 5));
        for (char& c : key) c = char(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        if (key != "BOOT2") continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        std::string_view v = line.substr(eq + 1);
        // O nome do executável: depois do último '\' ou ':' ("cdrom0:\SLUS_205.79;1"), até o ';'.
        const size_t cut = v.find_last_of("\\:");
        if (cut != std::string_view::npos) v.remove_prefix(cut + 1);
        const size_t semi = v.find(';');
        if (semi != std::string_view::npos) v = v.substr(0, semi);
        std::string id;
        for (char c : v) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '.') continue;
            if (c == '_') c = '-';
            id.push_back(char(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c));
        }
        return id;
    }
    return {};
}

DiscCheck check_disc(const DiscSource& src, const DiscTitle& title) {
    DiscCheck c;
    std::vector<uint8_t> cnf;
    const bool have_required = std::all_of(title.required.begin(), title.required.end(),
                                           [&](const std::string& f) { return src.exists(f); });
    if (src.read("SYSTEM.CNF", cnf)) {
        c.boot_id = disc_boot_id(std::string_view(reinterpret_cast<const char*>(cnf.data()), cnf.size()));
    }
    if (c.boot_id.empty()) {
        // Uma pasta só com os arquivos do jogo (ACTION.ELF e FILES.BIN extraídos, como antes): aceita sem a versão.
        if (src.kind() == DiscKind::Folder && have_required && !title.required.empty()) {
            c.status = DiscStatus::Ok;
            c.unverified = true;
            return c;
        }
        c.status = src.kind() == DiscKind::Folder && !src.exists("SYSTEM.CNF") ? DiscStatus::Missing : DiscStatus::NotPs2;
        return c;
    }
    if (c.boot_id != title.boot_id) {
        c.status = have_required ? DiscStatus::WrongVersion : DiscStatus::WrongGame;
        return c;
    }
    for (const std::string& f : title.required) {
        if (!src.exists(f)) {
            c.status = DiscStatus::Incomplete;
            c.missing = f;
            return c;
        }
    }
    c.status = DiscStatus::Ok;
    return c;
}

// ---------------------------------------------------------------- a procura

const char* disc_origin_name(DiscOrigin o) {
    switch (o) {
        case DiscOrigin::Command: return "linha_de_comando";
        case DiscOrigin::Packed: return "copia_pessoal";
        case DiscOrigin::ExeFolder: return "pasta_do_executavel";
        case DiscOrigin::WorkFolder: return "pasta_atual";
        case DiscOrigin::RepoFolder: return "pasta_de_cima";
        case DiscOrigin::Settings: return "ini";
        case DiscOrigin::Drive: return "leitor";
        case DiscOrigin::Picked: return "escolhido";
    }
    return "?";
}

DiscPlaces disc_places_here() {
    DiscPlaces p;
    p.exe = os::exe_path();
    p.exe_dir = parent_of(p.exe);
    p.cwd = os::current_dir();
    // As imagens .iso (e .cdr fora do Windows: disc_image_extension) ao lado do executável, em ordem de nome; num pacote
    // do macOS, ao lado do "Nightfire PC.app" (a pasta do executável é o Contents/MacOS de dentro dele). No Windows o
    // bundle_dir é vazio: a mesma pasta e as mesmas alocações de antes.
    {
        const std::wstring bundle = os::bundle_dir();
        const std::wstring beside_bundle = bundle.empty() ? std::wstring() : parent_of(bundle);
        const std::wstring& image_dir = bundle.empty() ? p.exe_dir : beside_bundle;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(image_dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::wstring ext = it->path().extension().wstring();
            for (wchar_t& ch : ext) ch = wchar_t(std::towlower(ch));
            if (disc_image_extension(ext)) p.exe_isos.push_back(it->path().wstring());
        }
        std::sort(p.exe_isos.begin(), p.exe_isos.end());
    }
    // As pastas de cima (build\Release -> build -> a raiz do repositório; build\x64\Release tem uma a mais).
    std::wstring d = p.exe_dir;
    for (int up = 0; up < 3; ++up) {
        d = parent_of(d);
        if (d.empty()) break;
        p.parents.push_back(d);
    }
    p.drives = optical_drives();
    return p;
}

std::vector<DiscCandidate> disc_candidates(const DiscPlaces& places) {
    std::vector<DiscCandidate> out;
    std::set<std::wstring> seen;
    const auto add = [&](const std::wstring& path, DiscOrigin origin) {
        if (path.empty()) return;
        std::wstring key = path;
        for (wchar_t& c : key) c = c == L'/' ? L'\\' : wchar_t(std::towlower(c));
        while (key.size() > 3 && key.back() == L'\\') key.pop_back();
        // A cópia pessoal é o próprio executável: não se confunde com a pasta dele.
        if (origin != DiscOrigin::Packed && !seen.insert(key).second) return;
        out.push_back(DiscCandidate{path, origin});
    };
    add(places.exe, DiscOrigin::Packed);
    add(places.exe_dir, DiscOrigin::ExeFolder);
    for (const std::wstring& iso : places.exe_isos) add(iso, DiscOrigin::ExeFolder);
    add(places.cwd, DiscOrigin::WorkFolder);
    for (const std::wstring& p : places.parents) add(p, DiscOrigin::RepoFolder);
    add(places.settings, DiscOrigin::Settings);
    for (const std::wstring& d : places.drives) add(d, DiscOrigin::Drive);
    return out;
}

namespace {

std::shared_ptr<DiscSource> default_open(const DiscCandidate& c, std::string* error) {
    QuietErrors quiet;
    if (c.origin == DiscOrigin::Packed) return open_packed_disc(c.path, error);
    // As pastas da procura (a do executável, a atual, as de cima) só valem com os arquivos do jogo: uma pasta qualquer
    // não é um "disco". A do .ini e os leitores valem como forem.
    if (c.origin == DiscOrigin::ExeFolder || c.origin == DiscOrigin::WorkFolder || c.origin == DiscOrigin::RepoFolder) {
        if (host_is_dir(c.path)) {
            if (!host_exists(os::join(c.path, L"FILES.BIN")) && !host_exists(os::join(c.path, L"SYSTEM.CNF"))) {
                return nullptr;
            }
            return open_disc_folder(c.path, DiscKind::Folder);
        }
    }
    if (c.origin == DiscOrigin::Drive) {
        // Um leitor vazio ou com um disco que não é de PS2: nada (sem SYSTEM.CNF).
        if (!host_exists(c.path + L"SYSTEM.CNF") && !host_exists(os::join(c.path, L"SYSTEM.CNF"))) return nullptr;
        return open_disc_folder(c.path, DiscKind::Drive);
    }
    return open_disc(c.path, error);
}

}  // namespace

DiscFound find_disc(const DiscPlaces& places, const DiscTitle& title, const DiscOpener& opener) {
    DiscFound found;
    for (const DiscCandidate& c : disc_candidates(places)) {
        std::string error;
        std::shared_ptr<DiscSource> src = opener ? opener(c, &error) : default_open(c, &error);
        if (src == nullptr) {
            // Só o que existia e não abriu conta como recusado (uma imagem estragada, o .ini apontando para nada).
            if (!error.empty() && c.origin != DiscOrigin::Packed) {
                DiscCheck bad;
                bad.status = error == "arquivo não encontrado" ? DiscStatus::Missing : DiscStatus::Unreadable;
                bad.detail = error;
                found.rejected.push_back({c.path, c.origin, bad});
            }
            continue;
        }
        const DiscCheck check = check_disc(*src, title);
        if (check.ok()) {
            found.source = src;
            found.origin = c.origin;
            found.check = check;
            return found;
        }
        found.rejected.push_back({c.path, c.origin, check});
    }
    return found;
}

DiscFound probe_disc(const std::wstring& path, DiscOrigin origin, const DiscTitle& title) {
    DiscFound found;
    found.origin = origin;
    std::string error;
    std::shared_ptr<DiscSource> src = open_disc(path, &error);
    if (src == nullptr) {
        found.check.status = error == "arquivo não encontrado" ? DiscStatus::Missing : DiscStatus::Unreadable;
        found.check.detail = error;
        found.rejected.push_back({path, origin, found.check});
        return found;
    }
    found.check = check_disc(*src, title);
    if (found.check.ok()) found.source = src;
    else found.rejected.push_back({path, origin, found.check});
    return found;
}

}  // namespace nf
