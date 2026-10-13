#pragma once
// As funções do C runtime da Microsoft que o código compartilhado usava (fopen_s, sscanf_s, _stricmp, _strnicmp,
// strncpy_s, _dupenv_s), atrás de nomes do port (só do port; docs/port.md, "Plataforma: interfaces e backends"). Com o
// MSVC cada uma é a função de antes (o mesmo comportamento: fopen_s não compartilha o arquivo, sscanf_s confere os
// tamanhos); com o clang do macOS (etapa 3), a função POSIX equivalente (o C11 Anexo K não existe lá).
//
// Caminhos largos (std::wstring) não passam por aqui: nf::os::open_file (platform/os.h).

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if !defined(_MSC_VER)
#include <strings.h>
#endif

// sscanf com conferência de tamanho no MSVC (sscanf_s); nos formatos do port (só números) é o sscanf.
#if defined(_MSC_VER)
#define NF_SSCANF sscanf_s
#else
#define NF_SSCANF std::sscanf
#endif

namespace nf {

// Comparação sem diferenciar maiúsculas (ASCII), como _stricmp/_strnicmp.
inline int str_icmp(const char* a, const char* b) {
#if defined(_MSC_VER)
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

inline int str_nicmp(const char* a, const char* b, size_t n) {
#if defined(_MSC_VER)
    return _strnicmp(a, b, n);
#else
    return strncasecmp(a, b, n);
#endif
}

// Um arquivo por caminho de 8 bits (a página de código do sistema no Windows; UTF-8 no macOS). Nulo se não abriu.
inline FILE* fopen_narrow(const char* path, const char* mode) {
#if defined(_MSC_VER)
    FILE* f = nullptr;
    if (fopen_s(&f, path, mode) != 0) return nullptr;
    return f;
#else
    return std::fopen(path, mode);
#endif
}

// Copia src em dst (size bytes com o 0 final), cortando o que não couber (strncpy_s com _TRUNCATE).
inline void str_copy_trunc(char* dst, size_t size, const char* src) {
#if defined(_MSC_VER)
    strncpy_s(dst, size, src, _TRUNCATE);
#else
    if (size == 0) return;
    size_t n = 0;
    while (n + 1 < size && src[n] != 0) {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = 0;
#endif
}

// Uma variável de ambiente de 8 bits (false: não existe).
inline bool get_env_narrow(const char* name, std::string* value) {
#if defined(_MSC_VER)
    char* v = nullptr;
    size_t len = 0;
    if (_dupenv_s(&v, &len, name) != 0 || v == nullptr) return false;
    if (value != nullptr) *value = v;
    std::free(v);
    return true;
#else
    const char* v = std::getenv(name);
    if (v == nullptr) return false;
    if (value != nullptr) *value = v;
    return true;
#endif
}

}  // namespace nf
