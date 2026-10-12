#pragma once
// Framework de testes mínimo (sem dependências externas).
// TEST(nome) { CHECK(cond); CHECK_NEAR(a, b, tol); }

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace nftest {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int n = 0;
    return n;
}

struct Register {
    Register(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void fail(const char* file, int line, const std::string& msg) {
    ++failures();
    std::printf("  FALHOU %s:%d: %s\n", file, line, msg.c_str());
}

}  // namespace nftest

#define NF_CAT2(a, b) a##b
#define NF_CAT(a, b) NF_CAT2(a, b)
#define TEST(name)                                                              \
    static void name();                                                         \
    static nftest::Register NF_CAT(reg_, name)(#name, name);                    \
    static void name()

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) nftest::fail(__FILE__, __LINE__, "CHECK(" #cond ")");      \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                   \
    do {                                                                        \
        const double nf_a = (a), nf_b = (b);                                    \
        if (!(std::fabs(nf_a - nf_b) <= (tol))) {                               \
            char nf_buf[256];                                                   \
            std::snprintf(nf_buf, sizeof(nf_buf), "%s = %.9g, %s = %.9g (tol %g)", #a, nf_a, #b, nf_b, \
                          double(tol));                                         \
            nftest::fail(__FILE__, __LINE__, nf_buf);                           \
        }                                                                       \
    } while (0)
