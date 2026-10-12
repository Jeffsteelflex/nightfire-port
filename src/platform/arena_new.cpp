// O operator new da versão de 64 bits: tudo que o port aloca com new (std::vector, std::string...) vem da arena, dentro
// da janela dos ponteiros de 32 bits do jogo (platform/arena.h, ptr32.h). Entra direto em cada executável x64 (não numa
// biblioteca), para o linker usar estas definições e não as do C runtime.

#include "platform/arena.h"

#if NF_PTR64

#include <new>

void* operator new(size_t n) { return nf::arena::alloc(n); }
void* operator new[](size_t n) { return nf::arena::alloc(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept {
    try {
        return nf::arena::alloc(n);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](size_t n, const std::nothrow_t&) noexcept {
    try {
        return nf::arena::alloc(n);
    } catch (...) {
        return nullptr;
    }
}
void* operator new(size_t n, std::align_val_t a) { return nf::arena::alloc(n, size_t(a)); }
void* operator new[](size_t n, std::align_val_t a) { return nf::arena::alloc(n, size_t(a)); }
void* operator new(size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try {
        return nf::arena::alloc(n, size_t(a));
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try {
        return nf::arena::alloc(n, size_t(a));
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* p) noexcept { nf::arena::free(p); }
void operator delete[](void* p) noexcept { nf::arena::free(p); }
void operator delete(void* p, size_t) noexcept { nf::arena::free(p); }
void operator delete[](void* p, size_t) noexcept { nf::arena::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { nf::arena::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { nf::arena::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { nf::arena::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { nf::arena::free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { nf::arena::free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { nf::arena::free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { nf::arena::free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { nf::arena::free(p); }

#endif
