#pragma once
// Interpretador mínimo de MIPS R5900 (o EE do PS2, little-endian) para rodar trechos do próprio
// ACTION.ELF que montam tabelas na partida: os construtores estáticos do GCC (`__CTOR_LIST__`),
// que preenchem weapon_data e outras tabelas que não estão prontas no .data do executável.
//
// Não é um emulador: só as instruções de que esse código precisa, decodificadas da codificação
// (opcode/funct), com delay slots e desvios "likely" (delay slot anulado quando não desvia). Os
// registradores têm 128 bits só para lq/sq; as operações comuns escrevem os 64 bits de baixo e
// preservam os de cima, como no EE. Qualquer instrução desconhecida, acesso fora da RAM ou execução
// fora das faixas de código permitidas para com MipsError (endereço + palavra crua); nada é ignorado.
// Chamadas a funções fora dessas faixas só valem para funções nativas registradas com hook().
//
// FPU (COP1) como a do EE: arredonda para zero, sem infinitos, NaN nem denormais (resultado grande
// demais vira ±FLT_MAX, pequeno demais vira ±0; entradas denormais valem 0, expoente 255 vira ±FLT_MAX).
//
// VU0 (COP2), opcional: com attach_vu0, lqc2/sqc2, qmfc2/qmtc2/cfc2/ctc2, as instruções em modo macro (vadd,
// vmula, vdiv, vlqi...) e vcallms/vcallmsr vão para um ps2::VuMachine (platform/vu_interp.h); o microprograma roda
// inteiro no vcallms (o EE do port não roda em paralelo com a VU0). Sem VU0 ligada, COP2 é instrução desconhecida.
// Também: SYNC e CACHE (nada), as MMI que copiam e entrelaçam quadwords (PEXTLW, PEXTUW, PCPYLD, PCPYUD, POR, PNOR)
// e SQRT.S, RSQRT.S, MAX.S e MIN.S.

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace ps2 {
class VuMachine;
}

namespace nf {

class Elf;

class MipsError : public std::runtime_error {
public:
    MipsError(const std::string& what, uint32_t pc, uint32_t word)
        : std::runtime_error(what), pc(pc), word(word) {}
    uint32_t pc;    // endereço da instrução que falhou
    uint32_t word;  // a instrução crua
};

class MipsMachine {
public:
    static constexpr uint32_t kRamSize = 32u << 20;  // 32 MB do EE, endereços físicos 0..kRamSize
    static constexpr uint32_t kStackTop = 0x01ff0000;
    static constexpr uint32_t kReturnAddress = 0x7fff0000;  // ra de call(): voltar aqui termina

    // Registradores pelo número da arquitetura.
    enum Reg : int {
        zero = 0, at = 1, v0 = 2, v1 = 3, a0 = 4, a1 = 5, a2 = 6, a3 = 7,
        t0 = 8, t1 = 9, t2 = 10, t3 = 11, t4 = 12, t5 = 13, t6 = 14, t7 = 15,
        s0 = 16, s1 = 17, s2 = 18, s3 = 19, s4 = 20, s5 = 21, s6 = 22, s7 = 23,
        t8 = 24, t9 = 25, k0 = 26, k1 = 27, gp = 28, sp = 29, s8 = 30, ra = 31,
    };

    using Native = std::function<void(MipsMachine&)>;

    MipsMachine();

    // RAM como o carregador do PS2 a deixa: os segmentos PT_LOAD copiados (o resto do memsz, .bss,
    // zerado); gp = _gp e sp = kStackTop, como o crt0 antes do __main.
    void load(const Elf& elf);

    std::vector<uint8_t>& ram() { return ram_; }
    const std::vector<uint8_t>& ram() const { return ram_; }
    // Ponteiro para [addr, addr + size) na RAM; MipsError fora dela.
    uint8_t* mem(uint32_t addr, uint32_t size);
    const uint8_t* mem(uint32_t addr, uint32_t size) const;
    uint32_t read32(uint32_t addr) const;
    void write32(uint32_t addr, uint32_t value);

    uint64_t gpr(int r) const { return gpr_[r].lo; }      // 64 bits de baixo
    uint64_t gpr_hi(int r) const { return gpr_[r].hi; }   // 64 bits de cima (só lq/sq os usam)
    void set_gpr(int r, uint64_t value);                  // 64 bits de baixo; r0 continua zero
    void set_gpr_hi(int r, uint64_t value);
    float fpr(int r) const;
    void set_fpr(int r, float value);
    uint32_t fpr_bits(int r) const { return fpr_[r]; }
    uint64_t lo() const { return lo_; }
    uint64_t hi() const { return hi_; }

    // Faixa [begin, end) que pode ser interpretada.
    void allow_code(uint32_t begin, uint32_t end);
    // Função nativa no lugar do código em addr: lê a0..a3/f12, escreve v0/f0; volta para ra.
    void hook(uint32_t addr, std::string name, Native fn);

    // Chama a função em entry com a0..a3 = args e ra = kReturnAddress, até ela voltar.
    // MipsError se algo der errado ou se passar de max_steps instruções.
    void call(uint32_t entry, const std::vector<uint32_t>& args = {}, uint64_t max_steps = 50'000'000);

    uint64_t steps() const { return steps_; }
    uint64_t native_calls() const { return native_calls_; }

    // A VU0 das instruções COP2 (nulo: COP2 desconhecida).
    void attach_vu0(ps2::VuMachine* vu) { vu0_ = vu; }
    uint64_t vu0_calls() const { return vu0_calls_; }

private:
    struct Gpr {
        uint64_t lo = 0, hi = 0;
    };
    struct Hook {
        std::string name;
        Native fn;
    };

    void step();
    void execute(uint32_t addr, uint32_t w, bool in_delay);
    void special(uint32_t addr, uint32_t w, bool in_delay);
    void regimm(uint32_t addr, uint32_t w, bool in_delay);
    void cop1(uint32_t addr, uint32_t w, bool in_delay);
    void cop2(uint32_t addr, uint32_t w, bool in_delay);
    void mmi(uint32_t addr, uint32_t w);
    void branch(uint32_t addr, uint32_t w, bool in_delay, bool taken, uint32_t target, bool likely);
    [[noreturn]] void fail(const std::string& what, uint32_t addr, uint32_t w) const;
    bool allowed(uint32_t addr) const;

    template <typename T>
    T load_mem(uint32_t addr, uint32_t pc, uint32_t w) const;
    template <typename T>
    void store_mem(uint32_t addr, T value, uint32_t pc, uint32_t w);

    std::vector<uint8_t> ram_;
    Gpr gpr_[32];
    uint32_t fpr_[32] = {};
    bool fcc_ = false;  // bit C do FCR31 (resultado do último c.cond.s)
    uint64_t lo_ = 0, hi_ = 0;
    uint32_t pc_ = 0, npc_ = 0;
    bool in_delay_ = false;  // a próxima instrução é o delay slot de um desvio
    uint64_t steps_ = 0, native_calls_ = 0;
    std::vector<std::pair<uint32_t, uint32_t>> code_;
    std::unordered_map<uint32_t, Hook> hooks_;
    ps2::VuMachine* vu0_ = nullptr;
    uint64_t vu0_calls_ = 0;
    uint32_t cmsar0_ = 0;
};

}  // namespace nf
