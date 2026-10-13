// A separação entre o código neutro e os backends de plataforma (docs/port.md, "Plataforma: interfaces e backends"): a
// mesma conferência de tools/check_platform.py, com as mesmas regras (tools/platform_rules.txt), sobre src/ e tests/.
// Um cabeçalho do sistema, uma API do Windows ou uma construção só do MSVC fora dos arquivos de backend falha aqui.

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "platform/os.h"
#include "test_framework.h"

namespace {

namespace fs = std::filesystem;

struct Rule {
    std::string kind, name;
    std::vector<std::string> allowed;
};

// Um padrão com * (qualquer trecho, inclusive '/'), como o fnmatch do Python.
bool glob(const char* p, const char* s) {
    if (*p == 0) return *s == 0;
    if (*p == '*') {
        for (const char* t = s;; ++t) {
            if (glob(p + 1, t)) return true;
            if (*t == 0) return false;
        }
    }
    return *s != 0 && *p == *s && glob(p + 1, s + 1);
}

bool any_glob(const std::vector<std::string>& globs, const std::string& rel) {
    for (const std::string& g : globs) {
        if (glob(g.c_str(), rel.c_str())) return true;
    }
    return false;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

bool matches(const std::string& pattern, const std::string& value, bool nocase) {
    const std::string p = nocase ? lower(pattern) : pattern;
    const std::string v = nocase ? lower(value) : value;
    if (!p.empty() && p.back() == '*') return v.compare(0, p.size() - 1, p, 0, p.size() - 1) == 0;
    return v == p;
}

bool load_rules(const fs::path& path, std::vector<std::string>* backends, std::vector<Rule>* rules) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line, section;
    while (std::getline(in, line)) {
        size_t a = line.find_first_not_of(" \t\r");
        if (a == std::string::npos || line[a] == '#') continue;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::istringstream words(line);
        std::vector<std::string> w;
        for (std::string x; words >> x;) w.push_back(x);
        if (w.empty()) continue;
        if (w[0].front() == '[') {
            section = w[0];
            continue;
        }
        if (section == "[backends]") {
            backends->push_back(w[0]);
        } else if (section == "[regras]" && w.size() >= 2) {
            rules->push_back(Rule{w[0], w[1], std::vector<std::string>(w.begin() + 2, w.end())});
        }
    }
    return true;
}

// Os comentários e os textos entre aspas viram espaços (as linhas ficam); o nome de um #include "..." fica.
std::string strip_code(const std::string& t) {
    std::string out;
    out.reserve(t.size());
    const size_t n = t.size();
    bool line_start = true, include_line = false;
    for (size_t i = 0; i < n;) {
        const char c = t[i];
        if (line_start) {
            size_t j = i;
            while (j < n && (t[j] == ' ' || t[j] == '\t')) ++j;
            include_line = false;
            if (j < n && t[j] == '#') {
                ++j;
                while (j < n && (t[j] == ' ' || t[j] == '\t')) ++j;
                include_line = t.compare(j, 7, "include") == 0;
            }
        }
        line_start = c == '\n';
        if (c == '/' && i + 1 < n && t[i + 1] == '/') {
            while (i < n && t[i] != '\n') {
                out.push_back(' ');
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < n && t[i + 1] == '*') {
            size_t j = t.find("*/", i + 2);
            j = j == std::string::npos ? n : j + 2;
            for (; i < j; ++i) out.push_back(t[i] == '\n' ? '\n' : ' ');
            continue;
        }
        if (c == 'R' && i + 1 < n && t[i + 1] == '"' && (i == 0 || !(std::isalnum(uint8_t(t[i - 1])) || t[i - 1] == '_'))) {
            const size_t k = t.find('(', i + 2);
            const std::string end = ")" + t.substr(i + 2, k - (i + 2)) + "\"";
            size_t j = t.find(end, k);
            j = j == std::string::npos ? n : j + end.size();
            for (; i < j; ++i) out.push_back(t[i] == '\n' ? '\n' : ' ');
            continue;
        }
        if ((c == '"' && !include_line) || c == '\'') {
            size_t j = i + 1;
            while (j < n && t[j] != c && t[j] != '\n') j += t[j] == '\\' ? 2 : 1;
            j = std::min(j + 1, n);
            out.push_back(c);
            for (size_t k = i + 1; k + 1 < j; ++k) out.push_back(' ');
            if (j - i >= 2) out.push_back(c);
            i = j;
            continue;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

void check_file(const std::string& rel, const std::string& text, const std::vector<Rule>& rules,
                std::vector<std::string>* found) {
    const std::string code = strip_code(text);
    std::istringstream lines(code);
    std::string line;
    for (int no = 1; std::getline(lines, line); ++no) {
        size_t p = line.find_first_not_of(" \t");
        if (p != std::string::npos && line[p] == '#') {
            size_t q = line.find_first_not_of(" \t", p + 1);
            const std::string rest = q == std::string::npos ? std::string() : line.substr(q);
            if (rest.compare(0, 7, "include") == 0) {
                const size_t a = rest.find_first_of("<\"");
                const size_t b = a == std::string::npos ? a : rest.find_first_of(">\"", a + 1);
                if (b != std::string::npos) {
                    std::string header = rest.substr(a + 1, b - a - 1);
                    for (char& ch : header) ch = ch == '\\' ? '/' : ch;
                    for (const Rule& r : rules) {
                        if (r.kind == "include" && matches(r.name, header, true) && !any_glob(r.allowed, rel)) {
                            found->push_back(rel + ":" + std::to_string(no) + ": include <" + header + ">");
                        }
                    }
                }
                continue;
            }
            if (rest.compare(0, 6, "pragma") == 0) {
                std::istringstream w(rest.substr(6));
                std::string name;
                w >> name;
                const size_t paren = name.find('(');
                if (paren != std::string::npos) name.resize(paren);
                for (const Rule& r : rules) {
                    if (r.kind == "pragma" && r.name == name && !any_glob(r.allowed, rel)) {
                        found->push_back(rel + ":" + std::to_string(no) + ": #pragma " + name);
                    }
                }
            }
        }
        std::set<std::string> tokens;
        for (size_t i = 0; i < line.size();) {
            if (std::isalpha(uint8_t(line[i])) || line[i] == '_') {
                size_t j = i;
                while (j < line.size() && (std::isalnum(uint8_t(line[j])) || line[j] == '_')) ++j;
                tokens.insert(line.substr(i, j - i));
                i = j;
            } else if (std::isdigit(uint8_t(line[i]))) {
                while (i < line.size() && (std::isalnum(uint8_t(line[i])) || line[i] == '_')) ++i;
            } else {
                ++i;
            }
        }
        for (const std::string& tok : tokens) {
            for (const Rule& r : rules) {
                // Primeiro o caractere (barato): a maioria dos identificadores não começa como nenhuma regra.
                if (r.kind != "token" || r.name.empty() || r.name[0] != tok[0]) continue;
                if (matches(r.name, tok, false) && !any_glob(r.allowed, rel)) {
                    found->push_back(rel + ":" + std::to_string(no) + ": " + tok);
                }
            }
        }
    }
}

}  // namespace

TEST(platform_rules_find_system_code) {
    // As regras acham o que devem num arquivo neutro e nada nos comentários e textos; num backend, nada.
    std::vector<std::string> backends;
    std::vector<Rule> rules;
    CHECK(load_rules(fs::path(NF_SOURCE_DIR) / "tools" / "platform_rules.txt", &backends, &rules));
    const std::string text =
        "#include <windows.h>\n// HWND num comentario\nconst char* s = \"DWORD\";\nHWND h; int k = VK_SPACE;\n"
        "#pragma warning(disable: 4324)\n#include \"render/renderer_d3d11.h\"\n";
    std::vector<std::string> found;
    check_file("src/engine/x.cpp", text, rules, &found);
    CHECK(found.size() == 5);
    found.clear();
    CHECK(any_glob(backends, "src/platform/os_win32.cpp") && any_glob(backends, "src/render/renderer_d3d11.cpp"));
    CHECK(!any_glob(backends, "src/platform/os.h") && !any_glob(backends, "src/app/app_main.cpp"));
}

TEST(platform_join_uses_the_system_separator) {
    // Os dois jeitos (nome fixo e nome montado) dão o mesmo caminho, com os '/' do nome trocados pelo separador do
    // sistema; os de dir ficam como estão.
    const std::wstring sep(1, nf::os::kPathSep);
    CHECK(nf::os::join(L"C:/dados", L"design/tokens.json") == L"C:/dados" + sep + L"design" + sep + L"tokens.json");
    CHECK(nf::os::join(L"C:/dados", std::wstring(L"design/tokens.json")) == nf::os::join(L"C:/dados", L"design/tokens.json"));
    CHECK(nf::os::join(L"", L"x") == sep + L"x");
    CHECK(nf::os::join(L"a", L"") == L"a" + sep);
}

TEST(platform_code_stays_behind_the_interfaces) {
    std::vector<std::string> backends;
    std::vector<Rule> rules;
    const fs::path root(NF_SOURCE_DIR);
    CHECK(load_rules(root / "tools" / "platform_rules.txt", &backends, &rules));
    CHECK(rules.size() > 50);
    std::vector<std::string> found;
    int files = 0;
    for (const char* top : {"src", "tests"}) {
        for (const auto& e : fs::recursive_directory_iterator(root / top)) {
            if (!e.is_regular_file()) continue;
            const std::string ext = e.path().extension().string();
            if (ext != ".cpp" && ext != ".h" && ext != ".inc") continue;
            std::string rel = fs::relative(e.path(), root).generic_string();
            ++files;
            if (any_glob(backends, rel)) continue;
            std::ifstream in(e.path(), std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            check_file(rel, ss.str(), rules, &found);
        }
    }
    for (const std::string& f : found) std::printf("  %s\n", f.c_str());
    CHECK(files > 300);
    CHECK(found.empty());
}
