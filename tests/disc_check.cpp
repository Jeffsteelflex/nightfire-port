// Confere o disco do jogo entre duas fontes (platform/disc_source.h), arquivo por arquivo e byte a byte: a imagem .iso
// contra os arquivos extraídos dela, o leitor de DVD (ou a imagem montada) contra a imagem, a cópia pessoal contra o
// disco. Não entra no ctest: precisa dos dados do usuário.
//
// Uso: nf_disc_check <fonte A> [fonte B]
//   Cada fonte é o que o nightfire.exe aceita em --disc: uma imagem .iso, uma pasta, um leitor ("D:") ou um executável
//   com a cópia pessoal. Com uma só: a versão (SYSTEM.CNF) e a lista. Com duas: cada arquivo de A procurado em B (sem
//   diferenciar maiúsculas, sem ";1") e comparado; B pode ter mais arquivos (a raiz do repositório tem o código).
//   Sai com 0 se todos os arquivos de A estão iguais em B.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/boot.h"
#include "platform/arena.h"
#include "platform/disc_source.h"

namespace {

std::wstring widen(const char* s) {
    const std::string n(s);
    return std::wstring(n.begin(), n.end());
}

std::shared_ptr<nf::DiscSource> open_or_say(const char* path) {
    std::string error;
    std::shared_ptr<nf::DiscSource> d = nf::open_disc(widen(path), &error);
    if (d == nullptr) std::printf("não abri %s: %s\n", path, error.c_str());
    return d;
}

void describe(const nf::DiscSource& d) {
    const nf::DiscCheck c = nf::check_disc(d, nf::nightfire_disc());
    const std::vector<nf::DiscEntry> files = d.list();
    uint64_t bytes = 0;
    for (const nf::DiscEntry& e : files) bytes += e.size;
    std::printf("%s: versao %s (%s%s), %zu arquivos, %llu bytes\n", d.describe().c_str(),
                c.boot_id.empty() ? "-" : c.boot_id.c_str(), nf::disc_status_name(c.status), c.unverified ? ", nao conferida" : "",
                files.size(), static_cast<unsigned long long>(bytes));
}

}  // namespace

int main_body(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::printf("uso: nf_disc_check <fonte A> [fonte B]\n");
        return 1;
    }
    const std::shared_ptr<nf::DiscSource> a = open_or_say(argv[1]);
    if (a == nullptr) return 2;
    describe(*a);
    if (argc < 3) {
        for (const nf::DiscEntry& e : a->list()) {
            std::printf("  %s %llu\n", e.path.c_str(), static_cast<unsigned long long>(e.size));
        }
        return 0;
    }
    const std::shared_ptr<nf::DiscSource> b = open_or_say(argv[2]);
    if (b == nullptr) return 2;
    describe(*b);
    size_t same = 0, different = 0, missing = 0;
    uint64_t bytes = 0;
    std::vector<uint8_t> ba(size_t(4) << 20), bb(size_t(4) << 20);
    for (const nf::DiscEntry& e : a->list()) {
        nf::DiscFile fa = a->open(e.path);
        nf::DiscFile fb = b->open(e.path);
        if (!fb.is_open()) {
            std::printf("  falta em B: %s\n", e.path.c_str());
            ++missing;
            continue;
        }
        bool equal = fa.is_open() && fa.size() == fb.size();
        uint64_t at = 0;
        while (equal && at < fa.size()) {
            const size_t n = size_t(std::min<uint64_t>(ba.size(), fa.size() - at));
            if (fa.read_at(at, ba.data(), n) != n || fb.read_at(at, bb.data(), n) != n ||
                std::memcmp(ba.data(), bb.data(), n) != 0) {
                equal = false;
                break;
            }
            at += n;
        }
        if (equal) {
            ++same;
            bytes += fa.size();
        } else {
            std::printf("  diferente: %s (A %llu bytes, B %llu bytes)\n", e.path.c_str(),
                        static_cast<unsigned long long>(fa.size()), static_cast<unsigned long long>(fb.size()));
            ++different;
        }
    }
    std::printf("%zu arquivos iguais (%llu bytes), %zu diferentes, %zu faltando em B\n", same,
                static_cast<unsigned long long>(bytes), different, missing);
    return different == 0 && missing == 0 ? 0 : 3;
}

int main(int argc, char** argv) {
    return nf::run_main([&] { return main_body(argc, argv); });
}
