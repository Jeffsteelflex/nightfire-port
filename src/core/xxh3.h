#pragma once
// XXH3 de 64 bits (semente 0, segredo padrão), a função de hash que o PCSX2 usa para nomear texturas
// de substituição. Implementação escalar própria a partir da especificação do xxHash
// (github.com/Cyan4973/xxHash, licença BSD de 2 cláusulas); o segredo padrão de 192 bytes vem de lá.

#include <cstddef>
#include <cstdint>

namespace nf {

uint64_t xxh3_64(const void* data, size_t len);

}  // namespace nf
