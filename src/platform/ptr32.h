#pragma once
// Ponteiros de 32 bits nas estruturas do jogo (docs/port.md, "64 bits").
//
// As estruturas do jogo (as do ACTION.ELF e do DRIVING.ELF, as dos arquivos do disco) têm o layout do PS2: cada campo
// de ponteiro tem 4 bytes, os tamanhos e deslocamentos são conferidos com static_assert e o código decompilado lê e
// escreve ponteiros dentro de blocos de dados carregados. Em 32 bits (x86, a versão de referência) um ponteiro nativo
// cabe nesses 4 bytes e nada muda. Em 64 bits um campo desses guarda o deslocamento do objeto dentro de uma janela de
// 4 GB que começa na imagem do programa (__ImageBase): a janela contém a imagem (o código, as globais, as strings
// literais), a arena (platform/arena.h: o heap do jogo, o operator new do port e a pilha onde o jogo roda), então todo
// endereço que o jogo pode guardar cabe em 32 bits; 0 é o ponteiro nulo (o começo da imagem nunca é apontado).
//
//  - nf::P32<T>: o tipo de um campo de ponteiro numa estrutura do jogo. Em x86 é T* (o código compila exatamente como
//    antes); em x64 é uma classe de 4 bytes que converte de e para T* sozinha (->, *, [], ++, +=, comparações e
//    aritmética pelo T*).
//  - nf::F32<R(A...)>: o mesmo para um ponteiro de função guardado numa estrutura do jogo ou numa tabela copiada do ELF
//    (o código fica na imagem, dentro da janela).
//  - nf::p32_u(p) e nf::p32_ptr<T>(v): ponteiro <-> palavra de 32 bits, para o código que guarda um ponteiro num
//    uint32_t (as palavras "unknown" que são ponteiros, as correções dos carregadores, as tabelas do ELF).
//  - NF_P32_CHECK (x64): guardar um ponteiro fora da janela (o heap do sistema, a pilha de outra thread, uma DLL) para
//    o programa com a pilha de chamadas.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#if UINTPTR_MAX > 0xffffffffu
#define NF_PTR64 1
#else
#define NF_PTR64 0
#endif

#ifndef NF_P32_CHECK
#define NF_P32_CHECK 0
#endif

#if NF_PTR64
// O início da imagem do executável (definido pelo linker da Microsoft): a base da janela.
extern "C" char __ImageBase;
#endif

namespace nf {

#if NF_PTR64

// A base da janela de 4 GB. Um endereço constante: já vale antes de qualquer construtor estático.
inline char* const kP32Base = &__ImageBase;

// Para o programa: um ponteiro fora da janela ia ser guardado num campo de 32 bits (platform/arena.cpp).
[[noreturn]] void p32_out_of_window(const void* p);

inline uint32_t p32_encode(const void* p) noexcept {
    if (p == nullptr) return 0;
    const uintptr_t d = reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(kP32Base);
#if NF_P32_CHECK
    if (d - 1 >= 0xffffffffu) p32_out_of_window(p);
#endif
    return static_cast<uint32_t>(d);
}
inline void* p32_decode(uint32_t v) noexcept { return v != 0 ? kP32Base + v : nullptr; }

template <class T>
class P32 {
public:
    P32() = default;
    P32(T* p) noexcept : v_(p32_encode(const_cast<const void*>(static_cast<const volatile void*>(p)))) {}
    // De outro P32 cujo ponteiro converte sozinho (derivada -> base, T* -> void*, T* -> const T*).
    template <class U, class = std::enable_if_t<!std::is_same_v<U, T> && std::is_convertible_v<U*, T*>>>
    P32(P32<U> o) noexcept : P32(static_cast<T*>(o.get())) {}

    T* get() const noexcept { return static_cast<T*>(p32_decode(v_)); }
    operator T*() const noexcept { return get(); }
    // Para os casts explícitos do código decompilado ((Drone_tag*)obj->data, static_cast<uint8_t*>(...)).
    template <class U, class = std::enable_if_t<!std::is_same_v<U, T>>>
    explicit operator U*() const noexcept {
        return (U*)(get());
    }
    T* operator->() const noexcept { return get(); }
    template <class U = T, class = std::enable_if_t<!std::is_void_v<U>>>
    U& operator*() const noexcept {
        return *get();
    }
    template <class U = T, class = std::enable_if_t<!std::is_void_v<U>>>
    U& operator[](std::ptrdiff_t i) const noexcept {
        return get()[i];
    }

    P32& operator=(T* p) noexcept {
        v_ = p32_encode(const_cast<const void*>(static_cast<const volatile void*>(p)));
        return *this;
    }
    P32& operator++() noexcept { return *this = get() + 1; }
    P32 operator++(int) noexcept {
        P32 old = *this;
        *this = get() + 1;
        return old;
    }
    P32& operator--() noexcept { return *this = get() - 1; }
    P32 operator--(int) noexcept {
        P32 old = *this;
        *this = get() - 1;
        return old;
    }
    P32& operator+=(std::ptrdiff_t n) noexcept { return *this = get() + n; }
    P32& operator-=(std::ptrdiff_t n) noexcept { return *this = get() - n; }

    // A palavra guardada (o deslocamento na janela; 0 = nulo).
    uint32_t raw() const noexcept { return v_; }
    static P32 from_raw(uint32_t v) noexcept {
        P32 p;
        p.v_ = v;
        return p;
    }

private:
    uint32_t v_;
};

template <class F>
class F32;
template <class R, class... A>
class F32<R(A...)> {
public:
    using Fn = R (*)(A...);
    F32() = default;
    F32(Fn f) noexcept : v_(p32_encode(reinterpret_cast<const void*>(f))) {}
    F32& operator=(Fn f) noexcept {
        v_ = p32_encode(reinterpret_cast<const void*>(f));
        return *this;
    }
    Fn get() const noexcept { return reinterpret_cast<Fn>(p32_decode(v_)); }
    operator Fn() const noexcept { return get(); }
    template <class... B>
    R operator()(B&&... b) const {
        return get()(static_cast<B&&>(b)...);
    }
    uint32_t raw() const noexcept { return v_; }
    static F32 from_raw(uint32_t v) noexcept {
        F32 f;
        f.v_ = v;
        return f;
    }

private:
    uint32_t v_;
};

inline uint32_t p32_u(const volatile void* p) noexcept { return p32_encode(const_cast<const void*>(p)); }
template <class T = void>
inline T* p32_ptr(uint32_t v) noexcept {
    return static_cast<T*>(p32_decode(v));
}
template <class F>
inline uint32_t p32_fn_u(F* f) noexcept {
    return p32_encode(reinterpret_cast<const void*>(f));
}
template <class F>
inline F* p32_fn(uint32_t v) noexcept {
    return reinterpret_cast<F*>(p32_decode(v));
}

#else  // 32 bits: o ponteiro nativo é o campo

template <class T>
using P32 = T*;
template <class F>
using F32 = F*;

inline uint32_t p32_u(const volatile void* p) noexcept {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}
template <class T = void>
inline T* p32_ptr(uint32_t v) noexcept {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(v));
}
template <class F>
inline uint32_t p32_fn_u(F* f) noexcept {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f));
}
template <class F>
inline F* p32_fn(uint32_t v) noexcept {
    return reinterpret_cast<F*>(static_cast<uintptr_t>(v));
}

#endif

// Lê e escreve uma palavra de ponteiro de 4 bytes num endereço qualquer (o código decompilado que lê um campo de
// ponteiro pelo deslocamento, std::memcpy(&p, base + 0x30, 4) no lugar de um campo P32).
template <class T = void>
inline T* p32_load(const void* addr) noexcept {
    uint32_t w;
    std::memcpy(&w, addr, 4);
    return p32_ptr<T>(w);
}
inline void p32_store(void* addr, const volatile void* p) noexcept {
    const uint32_t w = p32_u(p);
    std::memcpy(addr, &w, 4);
}

// O ponteiro nativo de um campo (para reinterpret_cast, auto*, o operador ?: com um T*): em x86 o próprio ponteiro.
template <class T>
inline T* raw(T* p) noexcept {
    return p;
}
#if NF_PTR64
template <class T>
inline T* raw(P32<T> p) noexcept {
    return p.get();
}
template <class F>
inline F* raw(F32<F> f) noexcept {
    return f.get();
}
#endif

// Os leitores de memória crua do código decompilado (rd<T>(base, off), at<T>(base, off)) leem sizeof(T) bytes: com T
// um ponteiro nativo, 8 bytes em x64 num campo de 4. Eles exigem P32<X> no lugar de X* (static_assert com isto).
template <class T>
inline constexpr bool is_native_ptr_v = NF_PTR64 && std::is_pointer_v<std::remove_cv_t<T>>;

static_assert(sizeof(P32<int>) == 4 && alignof(P32<int>) == 4);
static_assert(sizeof(F32<void(int)>) == 4);
static_assert(std::is_trivially_copyable_v<P32<int>> && std::is_trivially_default_constructible_v<P32<int>>);

}  // namespace nf

using nf::F32;
using nf::P32;
