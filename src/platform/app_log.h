#pragma once
// O registro do programa para quem testa (só do port; docs/port.md, "macOS: o pacote e a instalação"): as contas
// neutras, testadas em toda plataforma (tests/test_macos.cpp). O registro de verdade é o do programa de janela do macOS
// (platform/app_log_macos.h: o nightfire.log com a saída padrão e a de erro, o relatório de falhas incluído), que não
// tem console quando é aberto pelo Finder.
//
// Os jogadores anexam o registro em relatos públicos (um fórum): a pasta do usuário ("/Users/ana") vira "~" em todo o
// registro, e o nome da conta do Mac não vai junto. Sem alocar: a troca roda na thread que copia a saída, que não pode
// depender do operator new (o da arena, platform/arena.h) se outra thread cair segurando a trava dele.

#include <cstddef>
#include <cstring>

namespace nf {

// O caractere continua um nome de pasta ("/Users/ana" não é o começo de "/Users/anabela"): letras, dígitos, '.', '_',
// '-', e os bytes de um caractere UTF-8 que não é ASCII.
inline bool log_name_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return u >= 0x80 || (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '.' ||
           u == '_' || u == '-';
}

// Passa [p, p + n) para out(pedaço, tamanho) com cada home (a pasta do usuário, sem a barra no fim) trocada por "~",
// quando o que vem depois dela não continua o nome. Um home vazio ou de um caractere ("/") não troca nada.
template <class Out>
void log_mask_home(const char* p, size_t n, const char* home, size_t home_len, Out&& out) {
    size_t start = 0;
    if (home_len >= 2) {
        size_t i = 0;
        while (i + home_len <= n) {
            if (p[i] == home[0] && std::memcmp(p + i, home, home_len) == 0 &&
                (i + home_len == n || !log_name_char(p[i + home_len]))) {
                if (i > start) out(p + start, i - start);
                out("~", size_t(1));
                i += home_len;
                start = i;
                continue;
            }
            ++i;
        }
    }
    if (n > start) out(p + start, n - start);
}

}  // namespace nf
