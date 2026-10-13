#pragma once
// O que é de um compilador só, atrás de macros e funções pequenas (só do port; docs/port.md, "Plataforma: interfaces e
// backends"). O código compartilhado usa estes nomes; com o MSVC eles viram exatamente o que o código usava antes
// (__debugbreak, __forceinline, #pragma fenv_access, _controlfp_s, _BitScanForward64), com o clang (o macOS, etapa 3)
// os equivalentes dele.
//
// Atenção para a etapa 3: o clang junta multiplicação e soma de float numa instrução só (FMA) por padrão no ARM64
// (-ffp-contract=on), o que muda o último bit de contas como a * b + c. O código compartilhado tem de ser compilado com
// -ffp-contract=off, ou a física das missões de carro (nf_driving_check, bit a bit com o DRIVING.ELF) e a pontuação
// (PlrStat_GetScore) deixam de bater. O MSVC não contrai sem /fp:contract.

#include <cstdint>

#if defined(_MSC_VER)
#include <float.h>
#include <intrin.h>
#else
#include <cfenv>
#endif

// Para no depurador (ou termina o processo sem ele).
#if defined(_MSC_VER)
#define NF_DEBUGBREAK() __debugbreak()
#else
#define NF_DEBUGBREAK() __builtin_trap()
#endif

// Sempre em linha / nunca em linha. Alinhamento: alignas(n), do C++ (o port não usa __declspec(align)).
#if defined(_MSC_VER) && !defined(__clang__)
#define NF_FORCEINLINE __forceinline
#define NF_NOINLINE __declspec(noinline)
#else
#define NF_FORCEINLINE inline __attribute__((always_inline))
#define NF_NOINLINE __attribute__((noinline))
#endif

// O arquivo muda o modo de arredondamento (nf::RoundTowardZero): o compilador não pode dobrar nem reordenar contas de
// float supondo o modo padrão. No escopo do arquivo, antes das funções.
#if defined(_MSC_VER) && !defined(__clang__)
#define NF_FENV_ACCESS_ON __pragma(fenv_access(on))
#else
#define NF_FENV_ACCESS_ON _Pragma("STDC FENV_ACCESS ON")
#endif

namespace nf {

// O índice do bit 1 mais baixo (v != 0).
inline unsigned ctz64(uint64_t v) {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long b;
    _BitScanForward64(&b, v);
    return unsigned(b);
#elif defined(_MSC_VER)
    unsigned long b;
    if (_BitScanForward(&b, uint32_t(v))) return unsigned(b);
    _BitScanForward(&b, uint32_t(v >> 32));
    return unsigned(b) + 32;
#else
    return unsigned(__builtin_ctzll(v));
#endif
}

// Arredondamento para zero, como a FPU do EE, enquanto vive (no MSVC: x87 e SSE pelo _controlfp_s; no resto, o
// fesetround do C). O arquivo que a usa precisa de NF_FENV_ACCESS_ON. O modo volta ao sair.
class RoundTowardZero {
public:
    RoundTowardZero() {
#if defined(_MSC_VER)
        _controlfp_s(&old_, 0, 0);
        unsigned int cw = 0;
        _controlfp_s(&cw, _RC_CHOP, _MCW_RC);
#else
        old_ = unsigned(std::fegetround());
        std::fesetround(FE_TOWARDZERO);
#endif
    }
    ~RoundTowardZero() {
#if defined(_MSC_VER)
        unsigned int cw = 0;
        _controlfp_s(&cw, old_ & _MCW_RC, _MCW_RC);
#else
        std::fesetround(int(old_));
#endif
    }
    RoundTowardZero(const RoundTowardZero&) = delete;
    RoundTowardZero& operator=(const RoundTowardZero&) = delete;

private:
    unsigned int old_ = 0;
};

}  // namespace nf
