#include <cstdio>
#include <cstring>
#include <exception>

#include "platform/crash_report.h"
#include "platform/gamepad.h"
#include "test_framework.h"

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // sem buffer: a saída sobrevive a uma queda
    nf::install_crash_report();                // uma queda escreve a pilha (o teste que caiu é o último sem "ok")
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    // Nenhum teste lê um controle de verdade: cada um começa com uma fonte sem controles (os que simulam um põem a
    // deles). Sem isto, um controle ligado ao PC entrava nas ações dos testes (e vibrava).
    static nf::ScriptedPadSource no_pad;
    for (const auto& c : nftest::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        const int before = nftest::failures();
        no_pad = nf::ScriptedPadSource{};
        nf::set_pad_source(&no_pad);
        try {
            c.fn();
        } catch (const std::exception& e) {
            nftest::fail(__FILE__, __LINE__, std::string("exceção: ") + e.what());
        }
        ++run;
        std::printf("%s %s\n", nftest::failures() == before ? "ok    " : "FALHOU", c.name);
    }
    nf::set_pad_source(nullptr);
    std::printf("\n%d testes, %d falhas\n", run, nftest::failures());
    return nftest::failures() == 0 ? 0 : 1;
}
