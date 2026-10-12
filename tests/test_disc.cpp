// O disco do jogo no PC (platform/disc_source.h, platform/disc_pack.h) e a tela de configuração (ui/modern_setup.h):
// uma imagem ISO 9660 montada à mão, a versão do SYSTEM.CNF, a ordem da procura, a cópia pessoal ida e volta e a tela
// com entradas sintéticas. Sem os dados do jogo.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/pc_text.h"
#include "platform/disc_pack.h"
#include "platform/disc_source.h"
#include "test_framework.h"
#include "ui/modern_setup.h"

namespace {

namespace fs = std::filesystem;

// ---- uma imagem ISO 9660 à mão

struct IsoImage {
    std::vector<uint8_t> bytes;
    explicit IsoImage(uint32_t sectors) : bytes(size_t(sectors) * 2048, 0) {}
    uint8_t* sector(uint32_t s) { return bytes.data() + size_t(s) * 2048; }
};

void both32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        p[i] = uint8_t(v >> (8 * i));
        p[7 - i] = uint8_t(v >> (8 * i));
    }
}

// Um registro de diretório (o tamanho par, como pede o ISO 9660).
std::vector<uint8_t> record(const std::string& name, uint32_t lba, uint32_t size, uint8_t flags, uint8_t unit = 0) {
    const size_t n = name.size();
    std::vector<uint8_t> r(33 + n + ((33 + n) & 1), 0);
    r[0] = uint8_t(r.size());
    both32(&r[2], lba);
    both32(&r[10], size);
    r[25] = flags;
    r[26] = unit;
    r[28] = 1;
    r[31] = 1;
    r[32] = uint8_t(n);
    std::memcpy(&r[33], name.data(), n);
    return r;
}

// Um diretório em `sectors` setores a partir de lba: os registros em ordem, sem passar de um setor para o outro.
void write_dir(IsoImage& img, uint32_t lba, uint32_t sectors, uint32_t parent, const std::vector<std::vector<uint8_t>>& recs) {
    std::vector<std::vector<uint8_t>> all;
    all.push_back(record(std::string(1, '\0'), lba, sectors * 2048, 2));
    all.push_back(record(std::string(1, '\1'), parent, 2048, 2));
    all.insert(all.end(), recs.begin(), recs.end());
    size_t off = 0;
    for (const auto& r : all) {
        if (off % 2048 + r.size() > 2048) off = (off / 2048 + 1) * 2048;  // o resto do setor fica zerado
        std::memcpy(img.sector(lba) + off, r.data(), r.size());
        off += r.size();
    }
}

void put_file(IsoImage& img, uint32_t lba, const std::string& data) { std::memcpy(img.sector(lba), data.data(), data.size()); }

const std::string kCnf = "BOOT2 = cdrom0:\\SLUS_205.79;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n";

// A imagem de teste: a raiz em dois setores (52 arquivos vazios empurram os últimos registros para o segundo), PS2/ com
// um subdiretório, um arquivo sem extensão e um em duas partes contínuas.
IsoImage build_image(const std::string& cnf = kCnf, bool with_cnf = true) {
    IsoImage img(40);
    uint8_t* pvd = img.sector(16);
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    pvd[128] = 0x00;
    pvd[129] = 0x08;  // 2048 (os dois sentidos)
    pvd[130] = 0x08;
    pvd[131] = 0x00;
    const std::vector<uint8_t> root = record(std::string(1, '\0'), 18, 2 * 2048, 2);
    std::memcpy(pvd + 156, root.data(), 34);
    uint8_t* term = img.sector(17);
    term[0] = 255;
    std::memcpy(term + 1, "CD001", 5);
    term[6] = 1;
    std::vector<std::vector<uint8_t>> recs;
    if (with_cnf) recs.push_back(record("SYSTEM.CNF;1", 24, uint32_t(cnf.size()), 0));
    recs.push_back(record("ACTION.ELF;1", 25, 5, 0));
    recs.push_back(record("FILES.BIN;1", 26, 4096, 0));
    for (int i = 0; i < 52; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "F%02d.BIN;1", i);
        recs.push_back(record(name, 0, 0, 0));
    }
    recs.push_back(record("PS2", 20, 2048, 2));
    recs.push_back(record("README.;1", 28, 6, 0));
    // BIG em duas partes: 2048 bytes no setor 29 e 100 no 30.
    recs.push_back(record("BIG.;1", 29, 2048, 0x80));
    recs.push_back(record("BIG.;1", 30, 100, 0));
    write_dir(img, 18, 2, 18, recs);
    write_dir(img, 20, 1, 18, {record("SBINFO.SBI;1", 31, 3, 0), record("ENGLISH", 21, 2048, 2)});
    write_dir(img, 21, 1, 20, {record("A.TXT;1", 32, 2, 0)});
    if (with_cnf) put_file(img, 24, cnf);
    put_file(img, 25, "ELF!!");
    for (int i = 0; i < 4096; ++i) img.sector(26)[i] = uint8_t(i * 7);
    put_file(img, 28, "readme");
    for (int i = 0; i < 2148; ++i) img.sector(29)[i] = uint8_t(i ^ 0x5a);
    put_file(img, 31, "sbi");
    put_file(img, 32, "hi");
    return img;
}

fs::path temp_dir(const char* name) {
    const fs::path d = fs::temp_directory_path() / "nf_tests_disc" / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

void write_file(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    FILE* f = nullptr;
    _wfopen_s(&f, p.wstring().c_str(), L"wb");
    if (f == nullptr) return;
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

void write_bytes(const fs::path& p, const std::vector<uint8_t>& data) {
    write_file(p, std::string(reinterpret_cast<const char*>(data.data()), data.size()));
}

std::string read_disc(const nf::DiscSource& d, const std::string& path) {
    std::vector<uint8_t> b;
    if (!d.read(path, b)) return "<nada>";
    return std::string(b.begin(), b.end());
}

// Um disco de mentira num arquivo do PC: o SYSTEM.CNF e os arquivos pedidos (um IndexedDisc sobre um blob).
std::shared_ptr<nf::DiscSource> fake_disc(const fs::path& dir, const std::string& name, const std::string& cnf,
                                          bool game_files) {
    std::string blob;
    std::vector<nf::DiscEntry> entries;
    const auto add = [&](const std::string& path, const std::string& data) {
        entries.push_back(nf::DiscEntry{path, data.size(), blob.size()});
        blob += data;
    };
    if (!cnf.empty()) add("SYSTEM.CNF", cnf);
    if (game_files) {
        add("ACTION.ELF", "elf");
        add("FILES.BIN", "bin");
    }
    add("OTHER.DAT", "x");
    const fs::path host = dir / (name + ".blob");
    write_file(host, blob);
    return std::make_shared<nf::IndexedDisc>(nf::DiscKind::Image, host.wstring(), host.wstring(), std::move(entries));
}

}  // namespace

TEST(disc_iso9660_reads_a_hand_built_image) {
    IsoImage img = build_image();
    std::vector<nf::DiscEntry> files;
    std::string error;
    const nf::DiscReader reader = [&img](uint64_t off, void* dst, size_t n) -> size_t {
        if (off >= img.bytes.size()) return 0;
        const size_t got = std::min<size_t>(n, img.bytes.size() - size_t(off));
        std::memcpy(dst, img.bytes.data() + off, got);
        return got;
    };
    CHECK(nf::parse_iso9660(reader, img.bytes.size(), &files, &error));
    const auto find = [&](const std::string& p) -> const nf::DiscEntry* {
        for (const nf::DiscEntry& e : files) {
            if (nf::disc_normalize(e.path) == nf::disc_normalize(p)) return &e;
        }
        return nullptr;
    };
    // O registro do último arquivo vazio e os de depois estão no segundo setor da raiz.
    CHECK(find("F51.BIN") != nullptr);
    CHECK(find("README") != nullptr && find("README")->size == 6);
    CHECK(find("PS2/ENGLISH/A.TXT") != nullptr && find("PS2/ENGLISH/A.TXT")->offset == 32u * 2048);
    // O arquivo em duas partes contínuas vira um só.
    CHECK(find("BIG") != nullptr && find("BIG")->size == 2148 && find("BIG")->offset == 29u * 2048);
    CHECK(find("SYSTEM.CNF") != nullptr && find("system.cnf;1") != nullptr);

    // Pelo arquivo: open_disc reconhece a imagem; os nomes sem diferenciar maiúsculas, com ou sem ";1".
    const fs::path dir = temp_dir("iso");
    write_bytes(dir / "test.iso", img.bytes);
    const std::shared_ptr<nf::DiscSource> d = nf::open_disc((dir / "test.iso").wstring(), &error);
    CHECK(d != nullptr && d->kind() == nf::DiscKind::Image);
    if (d == nullptr) return;
    CHECK(read_disc(*d, "ps2/sbinfo.sbi") == "sbi");
    CHECK(read_disc(*d, "\\PS2\\ENGLISH\\A.TXT;1") == "hi");
    CHECK(read_disc(*d, "README") == "readme");
    CHECK(d->exists("FILES.BIN") && !d->exists("NOPE.BIN"));
    std::vector<uint8_t> big;
    CHECK(d->read("BIG", big) && big.size() == 2148 && big[2100] == uint8_t((2100 ^ 0x5a) & 0xff));
    // Um DiscFile: o cursor, a leitura no meio e o fim.
    nf::DiscFile f = d->open("FILES.BIN");
    CHECK(f.is_open() && f.size() == 4096);
    uint8_t b[8];
    f.seek(4090);
    CHECK(f.read(b, 8) == 6 && b[0] == uint8_t((4090 * 7) & 0xff) && f.tell() == 4096);
    CHECK(f.read_at(10, b, 2) == 2 && b[1] == uint8_t(11 * 7));
    CHECK(d->list("PS2").size() == 2 && d->list().size() == files.size());
    CHECK(d->folder() == dir.wstring());
}

TEST(disc_iso9660_refuses_broken_images) {
    std::vector<nf::DiscEntry> files;
    std::string error;
    IsoImage img = build_image();
    const auto parse = [&](IsoImage& i, uint64_t size) {
        const nf::DiscReader reader = [&i](uint64_t off, void* dst, size_t n) -> size_t {
            if (off >= i.bytes.size()) return 0;
            const size_t got = std::min<size_t>(n, i.bytes.size() - size_t(off));
            std::memcpy(dst, i.bytes.data() + off, got);
            return got;
        };
        return nf::parse_iso9660(reader, size, &files, &error);
    };
    // Cortada: um arquivo passa do fim.
    CHECK(!parse(img, 30u * 2048) && !error.empty());
    // Sem o descritor primário.
    IsoImage none(20);
    CHECK(!parse(none, none.bytes.size()));
    // Um arquivo intercalado (file unit size).
    IsoImage inter = build_image();
    write_dir(inter, 21, 1, 20, {record("A.TXT;1", 32, 2, 0, 4)});
    CHECK(!parse(inter, inter.bytes.size()));
}

TEST(disc_boot_id_from_system_cnf) {
    CHECK(nf::disc_boot_id(kCnf) == "SLUS-20579");
    CHECK(nf::disc_boot_id("BOOT2=cdrom0:\\SLUS_205.79;1\nVER=1.00\n") == "SLUS-20579");
    CHECK(nf::disc_boot_id("  boot2 = cdrom0:\\SLES_512.58;1\r\n") == "SLES-51258");
    CHECK(nf::disc_boot_id("BOOT = cdrom:\\SLUS_000.01;1\n") == "");  // PS1: BOOT, não BOOT2
    CHECK(nf::disc_boot_id("") == "");
}

TEST(disc_check_versions) {
    nf::DiscTitle title;
    title.boot_id = "SLUS-20579";
    title.required = {"ACTION.ELF", "FILES.BIN"};
    const fs::path dir = temp_dir("check");
    CHECK(nf::check_disc(*fake_disc(dir, "ok", kCnf, true), title).ok());
    const nf::DiscCheck pal = nf::check_disc(*fake_disc(dir, "pal", "BOOT2 = cdrom0:\\SLES_512.58;1\n", true), title);
    CHECK(pal.status == nf::DiscStatus::WrongVersion && pal.boot_id == "SLES-51258");
    CHECK(nf::check_disc(*fake_disc(dir, "other", "BOOT2 = cdrom0:\\SLUS_200.62;1\n", false), title).status ==
          nf::DiscStatus::WrongGame);
    CHECK(nf::check_disc(*fake_disc(dir, "nocnf", "", true), title).status == nf::DiscStatus::NotPs2);
    // A versão certa sem um arquivo do jogo.
    nf::DiscTitle more = title;
    more.required.push_back("PS2/SBINFO.SBI");
    const nf::DiscCheck inc = nf::check_disc(*fake_disc(dir, "inc", kCnf, true), more);
    CHECK(inc.status == nf::DiscStatus::Incomplete && inc.missing == "PS2/SBINFO.SBI");
    // Uma pasta só com os arquivos do jogo (sem SYSTEM.CNF), como antes: serve, sem a versão conferida.
    const fs::path legacy = dir / "legacy";
    write_file(legacy / "ACTION.ELF", "elf");
    write_file(legacy / "FILES.BIN", "bin");
    const nf::DiscCheck lc = nf::check_disc(*nf::open_disc_folder(legacy.wstring()), title);
    CHECK(lc.ok() && lc.unverified);
    const fs::path empty = dir / "empty";
    fs::create_directories(empty);
    CHECK(nf::check_disc(*nf::open_disc_folder(empty.wstring()), title).status == nf::DiscStatus::Missing);
    // Uma pasta com o SYSTEM.CNF: ";1" no nome acha o arquivo extraído sem ele.
    write_file(legacy / "SYSTEM.CNF", kCnf);
    CHECK(nf::open_disc_folder(legacy.wstring())->exists("SYSTEM.CNF;1"));
    CHECK(!nf::check_disc(*nf::open_disc_folder(legacy.wstring()), title).unverified);
}

TEST(disc_search_order) {
    nf::DiscPlaces p;
    p.exe = L"X:\\game\\nightfire.exe";
    p.exe_dir = L"X:\\game";
    p.exe_isos = {L"X:\\game\\a.iso"};
    p.cwd = L"x:\\GAME\\";  // a mesma pasta do executável: não repete
    p.parents = {L"X:\\", L"Y:\\repo"};
    p.settings = L"Y:\\nf.iso";
    p.drives = {L"D:\\"};
    const std::vector<nf::DiscCandidate> c = nf::disc_candidates(p);
    CHECK(c.size() == 7);
    if (c.size() != 7) return;
    CHECK(c[0].origin == nf::DiscOrigin::Packed && c[0].path == p.exe);
    CHECK(c[1].origin == nf::DiscOrigin::ExeFolder && c[1].path == p.exe_dir);
    CHECK(c[2].origin == nf::DiscOrigin::ExeFolder && c[2].path == p.exe_isos[0]);
    CHECK(c[3].origin == nf::DiscOrigin::RepoFolder && c[4].origin == nf::DiscOrigin::RepoFolder);
    CHECK(c[5].origin == nf::DiscOrigin::Settings && c[6].origin == nf::DiscOrigin::Drive);

    nf::DiscTitle title;
    title.boot_id = "SLUS-20579";
    title.required = {"ACTION.ELF", "FILES.BIN"};
    const fs::path dir = temp_dir("search");
    const auto pal = fake_disc(dir, "pal", "BOOT2 = cdrom0:\\SLES_512.58;1\n", true);
    const auto good = fake_disc(dir, "good", kCnf, true);
    const auto drive = fake_disc(dir, "drive", kCnf, true);
    std::vector<std::wstring> asked;
    const nf::DiscOpener opener = [&](const nf::DiscCandidate& cand, std::string* error) -> std::shared_ptr<nf::DiscSource> {
        asked.push_back(cand.path);
        if (cand.path == p.exe_isos[0]) return pal;
        if (cand.path == p.settings) return good;
        if (cand.path == p.drives[0]) return drive;
        if (cand.path == L"Y:\\repo") *error = "arquivo não encontrado";
        return nullptr;
    };
    const nf::DiscFound f = nf::find_disc(p, title, opener);
    // A imagem ao lado do executável é outra versão: recusada; o .ini vem antes do leitor.
    CHECK(f.ok() && f.source == good && f.origin == nf::DiscOrigin::Settings);
    CHECK(asked.size() == 6);  // o leitor nem é procurado
    CHECK(f.rejected.size() == 2);
    if (f.rejected.size() == 2) {
        CHECK(f.rejected[0].check.status == nf::DiscStatus::WrongVersion && f.rejected[0].origin == nf::DiscOrigin::ExeFolder);
        CHECK(f.rejected[1].check.status == nf::DiscStatus::Missing && f.rejected[1].path == L"Y:\\repo");
    }
    // Sem o .ini: o leitor.
    p.settings.clear();
    const nf::DiscFound g = nf::find_disc(p, title, opener);
    CHECK(g.ok() && g.source == drive && g.origin == nf::DiscOrigin::Drive);
}

TEST(disc_pack_round_trip) {
    const fs::path dir = temp_dir("pack");
    // O "executável": um PE de mentira (o formato não é conferido, só copiado).
    std::string exe(5000, '\0');
    exe[0] = 'M';
    exe[1] = 'Z';
    for (size_t i = 2; i < exe.size(); ++i) exe[i] = char(i * 13);
    write_file(dir / "game.exe", exe);
    const fs::path src = dir / "disc";
    write_file(src / "SYSTEM.CNF", kCnf);
    write_file(src / "ACTION.ELF", "the elf");
    write_file(src / "FILES.BIN", std::string(10000, 'F'));
    write_file(src / "PS2" / "SBINFO.SBI", "sbi");
    write_file(src / "PS2" / "ENGLISH" / "SB_0" / "SB_0.SFX", "sfx!");
    write_file(src / "MOVIES" / "30_FPS" / "07370048.PSS", std::string(3000, 'M'));
    write_file(src / "DRIVING" / "MISC.VIV", "not packed");
    write_file(dir / "hd" / "map.json", "{}");
    write_file(dir / "hd" / "svg" / "a.svg", "<svg/>");
    nf::DiscTitle title;
    title.boot_id = "SLUS-20579";
    title.required = {"ACTION.ELF", "FILES.BIN"};
    title.pack = {"ACTION.ELF", "SYSTEM.CNF", "FILES.BIN", "PS2/"};
    title.pack_optional = {"MOVIES/"};
    const auto disc = nf::open_disc_folder(src.wstring());
    const std::wstring exe_path = (dir / "game.exe").wstring();
    const nf::PackPlan no_movies = nf::pack_plan(*disc, title, exe_path, false);
    CHECK(no_movies.error == nf::PackError::None && no_movies.files.size() == 5);
    const nf::PackPlan plan = nf::pack_plan(*disc, title, exe_path, true, (dir / "hd").wstring(), "PORT/HD_UI/");
    CHECK(plan.files.size() == 6 && plan.extras.size() == 2 && plan.program == exe.size());
    const fs::path out = dir / "out" / "copy.exe";
    fs::create_directories(out.parent_path());
    nf::PackProgress progress;
    nf::PackError e = nf::PackError::None;
    std::string detail;
    CHECK(nf::pack_disc(*disc, title, plan, exe_path, out.wstring(), &progress, &e, &detail));
    CHECK(e == nf::PackError::None && progress.done == progress.total);
    CHECK(!fs::exists(out.wstring() + L".part"));
    // A cópia: o programa intacto no começo, os arquivos pelo índice, o DRIVING de fora.
    std::string copy;
    {
        std::vector<uint8_t> all;
        nf::DiscFile f = nf::DiscFile::open_host(out.wstring());
        CHECK(f.read_all(all));
        copy.assign(all.begin(), all.end());
    }
    CHECK(copy.compare(0, exe.size(), exe) == 0);
    std::string err;
    const auto packed = nf::open_packed_disc(out.wstring(), &err);
    CHECK(packed != nullptr && packed->kind() == nf::DiscKind::Packed && err.empty());
    if (packed == nullptr) return;
    CHECK(read_disc(*packed, "action.elf") == "the elf");
    CHECK(read_disc(*packed, "PS2/ENGLISH/SB_0/SB_0.SFX") == "sfx!");
    CHECK(read_disc(*packed, "MOVIES/30_FPS/07370048.PSS") == std::string(3000, 'M'));
    CHECK(read_disc(*packed, "FILES.BIN") == std::string(10000, 'F'));
    CHECK(!packed->exists("DRIVING/MISC.VIV"));
    CHECK(nf::check_disc(*packed, title).ok());
    CHECK(nf::packed_program_size(out.wstring()) == exe.size());
    // open_disc também a reconhece; um executável comum não é uma cópia.
    CHECK(nf::open_disc(out.wstring()) != nullptr);
    CHECK(nf::open_packed_disc(exe_path, &err) == nullptr && err.empty());
    // Os arquivos do port tirados para uma pasta (a raiz dos testes, não a do usuário).
    const std::wstring extracted = nf::extract_packed_extras(*packed, "PORT/HD_UI", (dir / "extract").wstring());
    CHECK(!extracted.empty() && fs::exists(fs::path(extracted) / "svg" / "a.svg") && fs::exists(fs::path(extracted) / "map.json"));
    // Uma cópia feita pela cópia: só o programa vai junto (sem aninhar a de antes).
    const fs::path out2 = dir / "out" / "copy2.exe";
    const nf::PackPlan plan2 = nf::pack_plan(*packed, title, out.wstring(), false);
    CHECK(plan2.program == exe.size());
    CHECK(nf::pack_disc(*packed, title, plan2, out.wstring(), out2.wstring(), nullptr, &e, &detail));
    const auto packed2 = nf::open_packed_disc(out2.wstring());
    CHECK(packed2 != nullptr && !packed2->exists("MOVIES/30_FPS/07370048.PSS") && read_disc(*packed2, "PS2/SBINFO.SBI") == "sbi");
    // O próprio executável e uma pasta com .git são recusados.
    CHECK(!nf::pack_disc(*disc, title, plan, exe_path, exe_path, nullptr, &e, &detail) && e == nf::PackError::SameFile);
    fs::create_directories(dir / "repo" / ".git");
    CHECK(nf::inside_git_tree((dir / "repo" / "sub" / "x.exe").wstring()));
    CHECK(!nf::inside_git_tree((dir / "out" / "x.exe").wstring()));
    CHECK(!nf::pack_disc(*disc, title, plan, exe_path, (dir / "repo" / "x.exe").wstring(), nullptr, &e, &detail) &&
          e == nf::PackError::InsideGitTree && !fs::exists(dir / "repo" / "x.exe"));
    // Sem SYSTEM.CNF a cópia não é feita (a versão não dá para conferir).
    fs::remove(src / "SYSTEM.CNF");
    CHECK(nf::pack_plan(*disc, title, exe_path, false).error == nf::PackError::NotVerified);
    CHECK(nf::default_pack_path(L"C:\\g\\nightfire.exe") == L"C:\\g\\nightfire-personal.exe");
    CHECK(nf::default_pack_path(L"C:\\g\\nightfire-personal.exe") == L"C:\\g\\nightfire-personal.exe");
}

TEST(setup_screen_rows_and_requests) {
    PcText_SetLanguage("en");
    nf::ui::SetupScreen s;
    const nf::ui::Canvas cv = nf::ui::make_canvas(1920, 1080);
    const nf::ui::Measure m;
    nf::ui::SetupView v;
    v.status = L"No disc selected yet.";
    s.open(v);
    const auto run = [&](nf::ui::Nav c) {
        s.command(c);
        s.frame(1.0f / 60.0f, nf::ui::UiInput{}, cv, m);
    };
    // A primeira montagem engole o que estava apertado.
    s.frame(1.0f / 60.0f, nf::ui::UiInput{}, cv, m);
    s.frame(1.0f / 60.0f, nf::ui::UiInput{}, cv, m);
    CHECK(s.row_count() == 4 && s.row_kind(0) == nf::ui::SetupScreen::Row::Browse);
    CHECK(s.row_locked(1) && s.row_locked(2) && !s.row_locked(3));  // PLAY e a cópia travados sem disco
    run(nf::ui::Nav::Select);
    CHECK(s.take_request().action == nf::ui::SetupAction::Browse);
    run(nf::ui::Nav::Down);
    run(nf::ui::Nav::Select);  // PLAY travado: só o aviso
    CHECK(s.focus() == 1 && s.take_request().action == nf::ui::SetupAction::None);
    // Dois leitores: o certo e outra versão (travado, com o motivo).
    v.drives = {{L"E:\\", false, L"another version"}, {L"F:\\", true, L""}};
    s.set_view(v);
    s.frame(1.0f / 60.0f, nf::ui::UiInput{}, cv, m);
    CHECK(s.row_count() == 6 && s.row_kind(1) == nf::ui::SetupScreen::Row::Drive && s.row_locked(1) && !s.row_locked(2));
    while (s.focus() != 2) run(s.focus() < 2 ? nf::ui::Nav::Down : nf::ui::Nav::Up);
    run(nf::ui::Nav::Select);
    const nf::ui::SetupRequest r = s.take_request();
    CHECK(r.action == nf::ui::SetupAction::Drive && r.drive == 1);
    // Um disco pronto: o foco vai para PLAY, que joga; a cópia também destrava.
    v.ready = true;
    v.can_pack = true;
    v.disc = L"C:\\games\\007 - Nightfire (USA).iso";
    v.status = nf::ui::setup_ready_text(nf::DiscCheck{nf::DiscStatus::Ok, "SLUS-20579"});
    s.set_view(v);
    s.focus_play();
    s.frame(1.0f / 60.0f, nf::ui::UiInput{}, cv, m);
    CHECK(s.row_kind(s.focus()) == nf::ui::SetupScreen::Row::Play && !s.row_locked(s.focus()));
    run(nf::ui::Nav::Select);
    CHECK(s.take_request().action == nf::ui::SetupAction::Play);
    run(nf::ui::Nav::Down);
    CHECK(s.row_kind(s.focus()) == nf::ui::SetupScreen::Row::Pack);
    // O desenho: o pedido do disco e o aviso da cópia (a linha com o foco).
    nf::ui::DrawList dl;
    s.build(dl, cv, m);
    CHECK(dl.find_text(L"SELECT YOUR 007: NIGHTFIRE DISC IMAGE (PS2, USA)") != nullptr);
    // O aviso vem quebrado em linhas na largura do painel.
    bool warned = false;
    for (const nf::ui::DrawCmd& c : dl.cmds) warned = warned || c.text.find(L"Do not share it.") != std::wstring::npos;
    CHECK(warned);
    run(nf::ui::Nav::Select);
    CHECK(s.take_request().action == nf::ui::SetupAction::Pack);
    // A caixa do andamento: escolher cancela; a de mensagem fecha com OK.
    s.show_progress(L"Making the copy", L"Copying your game… 10%", 0.1f);
    run(nf::ui::Nav::Select);
    CHECK(s.dialog_open() && s.take_request().action == nf::ui::SetupAction::CancelPack);
    s.hide_dialog();
    s.show_message(L"Copy made", L"Saved");
    s.frame(1.0f / 60.0f, nf::ui::UiInput{}, cv, m);
    run(nf::ui::Nav::Select);
    CHECK(!s.dialog_open() && s.take_request().action == nf::ui::SetupAction::None);
    run(nf::ui::Nav::Down);
    run(nf::ui::Nav::Select);
    CHECK(s.take_request().action == nf::ui::SetupAction::Quit);
}

TEST(setup_screen_mouse_and_texts) {
    PcText_SetLanguage("en");
    nf::ui::SetupScreen s;
    const nf::ui::Canvas cv = nf::ui::make_canvas(1920, 1080);
    const nf::ui::Measure m;
    s.open(nf::ui::SetupView{});
    nf::ui::UiInput in;
    for (int i = 0; i < 3; ++i) s.frame(1.0f / 60.0f, in, cv, m);
    // O clique na primeira linha (o retângulo da última montagem) pede a escolha da imagem.
    const nf::ui::Rect r = s.row_rect(0);
    in.mouse_inside = true;
    in.mouse_x = nf::ui::window_x(cv, r.cx());
    in.mouse_y = nf::ui::window_y(cv, r.cy());
    s.frame(1.0f / 60.0f, in, cv, m);
    in.lmb = true;
    s.frame(1.0f / 60.0f, in, cv, m);
    CHECK(s.take_request().action == nf::ui::SetupAction::Browse);
    // A janela de escolha aberta: nada responde.
    nf::ui::SetupView v;
    v.picking = true;
    s.set_view(v);
    in.lmb = false;
    s.frame(1.0f / 60.0f, in, cv, m);
    s.command(nf::ui::Nav::Select);
    s.frame(1.0f / 60.0f, in, cv, m);
    CHECK(s.take_request().action == nf::ui::SetupAction::None);
    // Os motivos, no idioma do port.
    nf::DiscCheck c;
    c.status = nf::DiscStatus::WrongVersion;
    c.boot_id = "SLES-51258";
    const std::wstring why = nf::ui::setup_problem_text(c);
    CHECK(why.find(L"SLES-51258") != std::wstring::npos && why.find(L"SLUS-20579") != std::wstring::npos);
    CHECK(nf::ui::setup_problem_text(nf::DiscCheck{nf::DiscStatus::Ok}).empty());
    CHECK(nf::ui::setup_size_text(uint64_t(1.9 * 1073741824.0)) == L"1.9 GB");
    CHECK(nf::ui::setup_size_text(650ull << 20) == L"650 MB");
    PcText_SetLanguage("pt");
    CHECK(nf::ui::setup_problem_text(c).find(L"outra versão") != std::wstring::npos);
    nf::ui::SetupScreen p;
    p.open(nf::ui::SetupView{});
    p.frame(1.0f / 60.0f, nf::ui::UiInput{}, cv, m);
    nf::ui::DrawList dl;
    p.build(dl, cv, m);
    CHECK(dl.find_text(L"ESCOLHA A IMAGEM DO SEU DISCO DE 007: NIGHTFIRE (PS2, EUA)") != nullptr);
    PcText_SetLanguage("en");
}
