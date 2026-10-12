#pragma once
// Interpretador das unidades vetoriais do PS2 (VU0 e VU1), genérico: roda microprogramas (o modo "micro",
// vcallms) e executa as instruções COP2 que o EE manda à VU0 (o modo "macro"). Feito para a física do
// DRIVING.ELF (os programas da VU0 no .vutext) e para os "render methods" do EAGL na VU1; não sabe nada de
// jogo nenhum (a "007 Anthology" do CLAUDE.md pode reusar).
//
// O que reproduz (o modelo do interpretador de VU do PCSX2, VUops.cpp, que segue o manual da Sony):
//  - os pares de 64 bits (instrução de cima, FMAC, e de baixo, FDIV/EFU/inteiros/memória/desvios), os campos
//    de destino (xyzw), o campo de difusão (bc), ACC, I (bit I: a palavra de baixo vai para I depois da de cima),
//    Q, P, R e os 16 registradores inteiros (vi00 vale sempre 0);
//  - no mesmo par, a de baixo lê os registradores de antes da de cima (que escreve depois) e, se as duas
//    escrevem o mesmo VF, a de baixo é descartada;
//  - a latência do FMAC (4 ciclos) só nos indicadores: o resultado de uma conta fica disponível já na seguinte
//    (o hardware para o pipeline até ele chegar; aqui só o contador de ciclos anda); os indicadores MAC, de
//    estado e de recorte (clip) que FMAND/FSAND/FCAND... leem são os das instruções de 4 ciclos ou mais atrás;
//  - Q (DIV 7 ciclos, SQRT 7, RSQRT 13) e P (a EFU, 11 a 54 ciclos): o resultado só aparece depois da latência;
//    ler Q ou P antes dá o valor velho, e WAITQ/WAITP ou uma segunda operação na mesma unidade esperam;
//  - os desvios com um delay slot, o bit E com o seu delay slot, um desvio no delay slot de outro;
//  - o desvio condicional que lê um VI escrito pela instrução inteira logo antes vê o valor velho (o "VI backup"
//    do PCSX2: IADD, IADDI, IADDIU, IAND, IOR, ISUB, ISUBIU, MTIR e os incrementos de LQI/LQD/SQI/SQD);
//  - XGKICK na VU1 guarda o pacote do GIF (as tags até a de EOP) para quem roda conferir;
//  - a aritmética do PS2 (platform/ps2_float.h): sem denormais, sem infinitos, truncamento.
// O que não reproduz: a transferência do XGKICK ao longo do tempo (aqui o pacote é copiado no XGKICK), os
// bits M, D e T (contados), os registradores da VU1 mapeados na memória da VU0 (os endereços dão a volta),
// e no modo macro a latência de Q (o EE espera sozinho; aqui o resultado é imediato, como no PCSX2).

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ps2 {

enum class VuUnit { VU0, VU1 };

class VuError : public std::runtime_error {
public:
    VuError(const std::string& what, uint32_t pc, uint32_t lower, uint32_t upper)
        : std::runtime_error(what), pc(pc), lower(lower), upper(upper) {}
    uint32_t pc;  // instrução (em unidades de 8 bytes)
    uint32_t lower, upper;
};

// Os registradores. Os VF e ACC guardam os bits de cada pista (x, y, z, w).
struct VuRegs {
    uint32_t vf[32][4] = {};
    uint32_t acc[4] = {};
    uint16_t vi[16] = {};
    uint32_t q = 0, p = 0, i = 0, r = 0x3f800000;
    // Indicadores como as instruções FMAND/FSAND/FCAND os leem (os "visíveis", 4 ciclos atrás).
    uint32_t mac = 0;     // 16 bits: Z (bits 0-3, w z y x), S (4-7), U (8-11), O (12-15)
    uint32_t status = 0;  // 12 bits: Z S U O I D e as versões persistentes (bits 6-11)
    uint32_t clip = 0;    // 24 bits: quatro resultados de CLIP de 6 bits
};

// Um pacote do GIF mandado por XGKICK (VU1).
struct GifKick {
    uint32_t pc = 0;       // instrução do XGKICK
    uint32_t address = 0;  // em quadwords na memória de dados
    std::vector<uint8_t> data;
};

struct VuStats {
    uint64_t instructions = 0;
    uint64_t cycles = 0;
    uint64_t stall_cycles = 0;
    uint64_t programs = 0;
    uint64_t branch_in_delay_slot = 0;
    uint64_t debug_bits = 0;      // bits D ou T vistos (ignorados)
    uint64_t address_wraps = 0;   // acessos fora da memória de dados (dão a volta)
};

class VuMachine {
public:
    explicit VuMachine(VuUnit unit);

    VuUnit unit() const { return unit_; }
    uint32_t micro_size() const { return uint32_t(micro_.size()); }  // bytes: 4 KB na VU0, 16 KB na VU1
    uint32_t data_size() const { return uint32_t(data_.size()); }
    uint8_t* micro() { return micro_.data(); }
    uint8_t* data() { return data_.data(); }
    const uint8_t* data() const { return data_.data(); }
    // Quadword n da memória de dados (dá a volta no tamanho).
    uint32_t* qword(uint32_t n);

    VuRegs& regs() { return r_; }
    const VuRegs& regs() const { return r_; }

    // Copia código para a memória de microprograma a partir da instrução instr (o MPG da VIF).
    void load_micro(uint32_t instr, const void* code, uint32_t bytes);

    // Roda a partir da instrução pc até o fim (o par depois do bit E). Para com VuError numa instrução
    // desconhecida ou depois de max_steps pares. Ao fim os pipelines são esvaziados (Q, P e os indicadores ficam
    // com os valores finais), como o vcallms seguinte ou um qmfc2.I enxergam.
    void run(uint32_t pc, uint64_t max_steps = 10'000'000);

    // Modo macro: uma instrução COP2 com o bit CO (os 25 bits de baixo de VADD, VMULA, VDIV, VLQI, VIADD...).
    // VCALLMS e VCALLMSR ficam com quem chama (devolve false para elas); true se executou.
    bool macro(uint32_t word);

    // XTOP/XITOP (VU1): o que a VIF daria.
    void set_top(uint16_t top, uint16_t itop) {
        top_ = top;
        itop_ = itop;
    }

    // A instrução (par de baixo e de cima) é conhecida pelo interpretador? (Para conferir a cobertura de um programa
    // sem rodá-lo.)
    static bool decodes(uint32_t lower, uint32_t upper);

    std::vector<GifKick>& kicks() { return kicks_; }
    const VuStats& stats() const { return stats_; }
    uint64_t cycle() const { return cycle_; }

private:
    struct Pending {  // um resultado a caminho (indicadores do FMAC, Q do FDIV, P da EFU)
        uint64_t start = 0;
        uint32_t latency = 0;
        uint32_t mac = 0, status = 0, clip = 0, value = 0;
        bool active = false;
    };

    // Uma instrução de cima: calcula sem escrever (a de baixo do mesmo par lê os valores de antes).
    struct UpperResult {
        int reg = -1;       // VF de destino (-1: nenhum)
        bool to_acc = false;
        uint32_t mask = 0;  // campo de destino (bit 3 = x ... bit 0 = w)
        uint32_t value[4] = {};
        bool sets_mac = false;
        bool sets_clip = false;
        uint32_t mac = 0;
    };

    UpperResult upper(uint32_t w) const;
    void commit_upper(const UpperResult& u);
    // Executa a de baixo; branch_target >= 0 se desviou. ret = true se a instrução existe.
    void lower(uint32_t w, uint32_t pc, int64_t& branch_target);
    void lower_special(uint32_t w);
    bool lower_writes_vf(uint32_t w, int& reg) const;
    void stall_for_lower(uint32_t w);
    void flush_pipes();
    void flush_all();
    void backup_vi(int reg);
    uint16_t read_vi_for_branch(int reg) const;
    void set_vi(int reg, uint16_t v);
    uint32_t data_word(uint32_t qw, int field);
    void xgkick(uint32_t pc, uint32_t address);
    [[noreturn]] void fail(const char* what, uint32_t pc, uint32_t lo, uint32_t up) const;

    VuUnit unit_;
    std::vector<uint8_t> micro_, data_;
    uint32_t micro_mask_, data_mask_;  // em instruções e em quadwords
    VuRegs r_;
    // Indicadores internos (os da última instrução que os mudou), que entram no pipeline do FMAC.
    uint32_t mac_now_ = 0, status_now_ = 0, clip_now_ = 0;
    std::vector<Pending> fmac_;
    Pending fdiv_, efu_;
    uint64_t cycle_ = 0;
    int vi_backup_cycles_ = 0, vi_backup_reg_ = -1;
    uint16_t vi_backup_value_ = 0;
    uint16_t top_ = 0, itop_ = 0;
    std::vector<GifKick> kicks_;
    VuStats stats_;
};

// O campo de destino de uma instrução (bits 21-24: x = 8, y = 4, z = 2, w = 1).
inline uint32_t vu_dest(uint32_t w) { return (w >> 21) & 0xf; }

}  // namespace ps2
