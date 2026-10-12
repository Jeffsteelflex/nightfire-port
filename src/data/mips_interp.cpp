#include "data/mips_interp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "data/elf.h"
#include "platform/vu_interp.h"

namespace nf {

namespace {

constexpr uint64_t sext32(uint32_t v) { return uint64_t(int64_t(int32_t(v))); }
constexpr uint64_t sext16(uint32_t v) { return uint64_t(int64_t(int16_t(uint16_t(v)))); }

std::string hex8(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08x", v);
    return buf;
}

uint32_t fbits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

float ffrom(uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// Operando como a FPU do EE o enxerga: denormal vale ±0 e expoente 255 (que no EE é um número
// normal, maior que FLT_MAX) satura em ±FLT_MAX.
double fin(uint32_t b) {
    const uint32_t e = (b >> 23) & 0xff;
    if (e == 0) return (b & 0x80000000u) ? -0.0 : 0.0;
    if (e == 0xff) b = (b & 0x80000000u) | 0x7f7fffffu;
    return double(ffrom(b));
}

// Sem denormais: resultado pequeno demais vira ±0.
uint32_t flush(uint32_t b) { return ((b >> 23) & 0xff) == 0 ? (b & 0x80000000u) : b; }

// Valor exato x (em double) arredondado para zero em float, saturando em ±FLT_MAX.
uint32_t round_zero(double x) {
    if (x == 0.0) return std::signbit(x) ? 0x80000000u : 0u;
    float f = static_cast<float>(x);  // ao mais próximo (pode dar infinito)
    if (std::fabs(double(f)) > std::fabs(x)) f = std::nextafter(f, 0.0f);
    return flush(fbits(f));
}

// Soma de floats arredondada para zero. A soma em double é exata enquanto os expoentes diferem
// em até 29; acima disso o menor operando só decide o lado, e o caso em que ele some de vez no
// double (s == a) é tratado à parte: com sinal oposto, o resultado é o float anterior a a.
uint32_t add_zero(double a, double b) {
    const double s = a + b;
    if (a != 0.0 && b != 0.0) {
        if (s == a || s == b) {
            const double big = s == a ? a : b;
            const double tiny = s == a ? b : a;
            float f = static_cast<float>(big);
            if (std::signbit(big) != std::signbit(tiny)) f = std::nextafter(f, 0.0f);
            return flush(fbits(f));
        }
    }
    return round_zero(s);
}

}  // namespace

MipsMachine::MipsMachine() : ram_(kRamSize, 0) { gpr_[sp].lo = kStackTop; }

void MipsMachine::load(const Elf& elf) {
    std::fill(ram_.begin(), ram_.end(), uint8_t(0));
    const ByteView file = elf.file();
    for (const ElfSegment& s : elf.segments()) {
        if (s.type != 1) continue;  // PT_LOAD
        if (s.memsz < s.filesz || s.vaddr > kRamSize || s.memsz > kRamSize - s.vaddr) {
            throw MipsError("segmento do ELF fora da RAM", s.vaddr, 0);
        }
        const ByteView bytes = sub(file, s.offset, s.filesz);
        std::memcpy(ram_.data() + s.vaddr, bytes.data(), bytes.size());
    }
    for (Gpr& g : gpr_) g = {};
    std::memset(fpr_, 0, sizeof(fpr_));
    fcc_ = false;
    lo_ = hi_ = 0;
    if (auto g = elf.find("_gp")) gpr_[gp].lo = sext32(g->value);
    gpr_[sp].lo = kStackTop;
}

uint8_t* MipsMachine::mem(uint32_t addr, uint32_t size) {
    if (addr > kRamSize || size > kRamSize - addr) throw MipsError("acesso fora da RAM em " + hex8(addr), addr, 0);
    return ram_.data() + addr;
}

const uint8_t* MipsMachine::mem(uint32_t addr, uint32_t size) const {
    if (addr > kRamSize || size > kRamSize - addr) throw MipsError("acesso fora da RAM em " + hex8(addr), addr, 0);
    return ram_.data() + addr;
}

uint32_t MipsMachine::read32(uint32_t addr) const {
    uint32_t v;
    std::memcpy(&v, mem(addr, 4), 4);
    return v;
}

void MipsMachine::write32(uint32_t addr, uint32_t value) { std::memcpy(mem(addr, 4), &value, 4); }

void MipsMachine::set_gpr(int r, uint64_t value) {
    if (r != 0) gpr_[r].lo = value;
}

void MipsMachine::set_gpr_hi(int r, uint64_t value) {
    if (r != 0) gpr_[r].hi = value;
}

float MipsMachine::fpr(int r) const { return ffrom(fpr_[r]); }
void MipsMachine::set_fpr(int r, float value) { fpr_[r] = fbits(value); }

void MipsMachine::allow_code(uint32_t begin, uint32_t end) { code_.push_back({begin, end}); }

void MipsMachine::hook(uint32_t addr, std::string name, Native fn) { hooks_[addr] = Hook{std::move(name), std::move(fn)}; }

bool MipsMachine::allowed(uint32_t addr) const {
    for (const auto& [b, e] : code_) {
        if (addr >= b && addr < e) return true;
    }
    return false;
}

void MipsMachine::fail(const std::string& what, uint32_t addr, uint32_t w) const {
    throw MipsError("MIPS: " + what + " em " + hex8(addr) + " (instrução " + hex8(w) + ")", addr, w);
}

void MipsMachine::call(uint32_t entry, const std::vector<uint32_t>& args, uint64_t max_steps) {
    if (args.size() > 4) throw MipsError("MIPS: mais de 4 argumentos", entry, 0);
    for (size_t i = 0; i < args.size(); ++i) gpr_[a0 + i].lo = sext32(args[i]);
    gpr_[ra].lo = sext32(kReturnAddress);
    pc_ = entry;
    npc_ = entry + 4;
    in_delay_ = false;
    uint32_t from = entry;
    const uint64_t limit = steps_ + max_steps;
    while (pc_ != kReturnAddress || in_delay_) {
        if (steps_ >= limit) fail("limite de instruções estourado", pc_, 0);
        if (!in_delay_) {
            auto h = hooks_.find(pc_);
            if (h != hooks_.end()) {
                h->second.fn(*this);
                ++native_calls_;
                gpr_[0] = {};
                pc_ = uint32_t(gpr_[ra].lo);
                npc_ = pc_ + 4;
                continue;
            }
        }
        if ((pc_ & 3) != 0 || !allowed(pc_)) {
            throw MipsError("MIPS: execução fora do código permitido em " + hex8(pc_) + " (vindo de " + hex8(from) +
                                "; chamada a função sem hook?)",
                            pc_, 0);
        }
        from = pc_;
        step();
    }
}

void MipsMachine::step() {
    const uint32_t addr = pc_;
    const bool in_delay = in_delay_;
    in_delay_ = false;
    const uint32_t w = read32(addr);
    pc_ = npc_;
    npc_ = pc_ + 4;
    execute(addr, w, in_delay);
    gpr_[0] = {};
    ++steps_;
}

void MipsMachine::branch(uint32_t addr, uint32_t w, bool in_delay, bool taken, uint32_t target, bool likely) {
    if (in_delay) fail("desvio dentro de um delay slot", addr, w);
    if (taken) {
        npc_ = target;
        in_delay_ = true;
    } else if (likely) {
        // Desvio "likely" não tomado: o delay slot é anulado.
        pc_ = addr + 8;
        npc_ = addr + 12;
    } else {
        in_delay_ = true;
    }
}

template <typename T>
T MipsMachine::load_mem(uint32_t addr, uint32_t pc, uint32_t w) const {
    if (addr % sizeof(T)) fail("leitura desalinhada de " + hex8(addr), pc, w);
    if (addr > kRamSize || sizeof(T) > kRamSize - addr) fail("leitura fora da RAM em " + hex8(addr), pc, w);
    T v;
    std::memcpy(&v, ram_.data() + addr, sizeof(T));
    return v;
}

template <typename T>
void MipsMachine::store_mem(uint32_t addr, T value, uint32_t pc, uint32_t w) {
    if (addr % sizeof(T)) fail("escrita desalinhada em " + hex8(addr), pc, w);
    if (addr > kRamSize || sizeof(T) > kRamSize - addr) fail("escrita fora da RAM em " + hex8(addr), pc, w);
    std::memcpy(ram_.data() + addr, &value, sizeof(T));
}

void MipsMachine::execute(uint32_t addr, uint32_t w, bool in_delay) {
    const uint32_t op = w >> 26;
    const int rs = int((w >> 21) & 31);
    const int rt = int((w >> 16) & 31);
    const uint32_t imm = w & 0xffff;
    const uint64_t simm = sext16(imm);
    const uint32_t ea = uint32_t(gpr_[rs].lo) + uint32_t(simm);  // endereço efetivo de loads/stores
    const uint32_t btarget = addr + 4 + uint32_t(simm << 2);
    const int64_t s = int64_t(gpr_[rs].lo);
    uint64_t& t = gpr_[rt].lo;

    switch (op) {
        case 0x00: special(addr, w, in_delay); return;
        case 0x01: regimm(addr, w, in_delay); return;
        case 0x02:  // J
        case 0x03: {  // JAL
            if (in_delay) fail("desvio dentro de um delay slot", addr, w);
            const uint32_t target = ((addr + 4) & 0xf0000000u) | ((w & 0x03ffffffu) << 2);
            if (op == 0x03) gpr_[ra].lo = sext32(addr + 8);
            branch(addr, w, in_delay, true, target, false);
            return;
        }
        case 0x04: branch(addr, w, in_delay, gpr_[rs].lo == gpr_[rt].lo, btarget, false); return;  // BEQ
        case 0x05: branch(addr, w, in_delay, gpr_[rs].lo != gpr_[rt].lo, btarget, false); return;  // BNE
        case 0x06: branch(addr, w, in_delay, s <= 0, btarget, false); return;                      // BLEZ
        case 0x07: branch(addr, w, in_delay, s > 0, btarget, false); return;                       // BGTZ
        case 0x14: branch(addr, w, in_delay, gpr_[rs].lo == gpr_[rt].lo, btarget, true); return;   // BEQL
        case 0x15: branch(addr, w, in_delay, gpr_[rs].lo != gpr_[rt].lo, btarget, true); return;   // BNEL
        case 0x16: branch(addr, w, in_delay, s <= 0, btarget, true); return;                       // BLEZL
        case 0x17: branch(addr, w, in_delay, s > 0, btarget, true); return;                        // BGTZL

        case 0x08:                                                                  // ADDI (sem a exceção de estouro)
        case 0x09: t = sext32(uint32_t(s) + uint32_t(simm)); return;                // ADDIU
        case 0x0a: t = s < int64_t(simm) ? 1 : 0; return;                           // SLTI
        case 0x0b: t = uint64_t(s) < simm ? 1 : 0; return;                          // SLTIU
        case 0x0c: t = uint64_t(s) & imm; return;                                   // ANDI
        case 0x0d: t = uint64_t(s) | imm; return;                                   // ORI
        case 0x0e: t = uint64_t(s) ^ imm; return;                                   // XORI
        case 0x0f: t = sext32(imm << 16); return;                                   // LUI
        case 0x19: t = uint64_t(s) + simm; return;                                  // DADDIU
        case 0x11: cop1(addr, w, in_delay); return;
        case 0x12: cop2(addr, w, in_delay); return;
        case 0x1c: mmi(addr, w); return;
        case 0x2f: return;  // CACHE (sem cache aqui)
        case 0x36: {        // LQC2
            if (!vu0_) fail("LQC2 sem VU0 ligada", addr, w);
            const uint32_t q = ea & ~15u;
            if (rt != 0) std::memcpy(vu0_->regs().vf[rt], mem(q, 16), 16);
            return;
        }
        case 0x3e: {  // SQC2
            if (!vu0_) fail("SQC2 sem VU0 ligada", addr, w);
            std::memcpy(mem(ea & ~15u, 16), vu0_->regs().vf[rt], 16);
            return;
        }

        // Loads e stores alinhados.
        case 0x20: t = uint64_t(int64_t(load_mem<int8_t>(ea, addr, w))); return;    // LB
        case 0x24: t = load_mem<uint8_t>(ea, addr, w); return;                      // LBU
        case 0x21: t = uint64_t(int64_t(load_mem<int16_t>(ea, addr, w))); return;   // LH
        case 0x25: t = load_mem<uint16_t>(ea, addr, w); return;                     // LHU
        case 0x23: t = sext32(load_mem<uint32_t>(ea, addr, w)); return;             // LW
        case 0x27: t = load_mem<uint32_t>(ea, addr, w); return;                     // LWU
        case 0x37: t = load_mem<uint64_t>(ea, addr, w); return;                     // LD
        case 0x28: store_mem<uint8_t>(ea, uint8_t(t), addr, w); return;             // SB
        case 0x29: store_mem<uint16_t>(ea, uint16_t(t), addr, w); return;           // SH
        case 0x2b: store_mem<uint32_t>(ea, uint32_t(t), addr, w); return;           // SW
        case 0x3f: store_mem<uint64_t>(ea, t, addr, w); return;                     // SD
        case 0x31: fpr_[rt] = load_mem<uint32_t>(ea, addr, w); return;              // LWC1
        case 0x39: store_mem<uint32_t>(ea, fpr_[rt], addr, w); return;              // SWC1
        case 0x1e: {  // LQ: o EE ignora os 4 bits de baixo do endereço
            const uint32_t q = ea & ~15u;
            const uint64_t lo = load_mem<uint64_t>(q, addr, w), hi = load_mem<uint64_t>(q + 8, addr, w);
            if (rt != 0) gpr_[rt] = {lo, hi};
            return;
        }
        case 0x1f: {  // SQ
            const uint32_t q = ea & ~15u;
            const Gpr v = gpr_[rt];
            store_mem<uint64_t>(q, v.lo, addr, w);
            store_mem<uint64_t>(q + 8, v.hi, addr, w);
            return;
        }

        // Acessos desalinhados (little-endian). "Esquerda" é a parte mais significativa do
        // registrador, que fica nos endereços mais altos: lwl/ldl em addr carregam os bytes de
        // [alinhado, addr] no topo do registrador; lwr/ldr carregam [addr, fim da palavra] na base.
        case 0x22: {  // LWL
            const uint32_t sh = ea & 3, m = load_mem<uint32_t>(ea & ~3u, addr, w);
            const uint32_t keep = 0x00ffffffu >> (sh * 8);
            t = sext32((uint32_t(t) & keep) | (m << (24 - sh * 8)));
            return;
        }
        case 0x26: {  // LWR: só estende o sinal quando a palavra inteira foi carregada
            const uint32_t sh = ea & 3, m = load_mem<uint32_t>(ea & ~3u, addr, w);
            const uint32_t keep = sh == 0 ? 0u : 0xffffffffu << ((4 - sh) * 8);
            const uint32_t v = (uint32_t(t) & keep) | (m >> (sh * 8));
            t = sh == 0 ? sext32(v) : ((t & 0xffffffff00000000ull) | v);
            return;
        }
        case 0x1a: {  // LDL
            const uint32_t sh = ea & 7;
            const uint64_t m = load_mem<uint64_t>(ea & ~7u, addr, w);
            const uint64_t keep = sh == 7 ? 0ull : 0x00ffffffffffffffull >> (sh * 8);
            t = (t & keep) | (m << (56 - sh * 8));
            return;
        }
        case 0x1b: {  // LDR
            const uint32_t sh = ea & 7;
            const uint64_t m = load_mem<uint64_t>(ea & ~7u, addr, w);
            const uint64_t keep = sh == 0 ? 0ull : ~0ull << ((8 - sh) * 8);
            t = (t & keep) | (m >> (sh * 8));
            return;
        }
        case 0x2a: {  // SWL
            const uint32_t sh = ea & 3, a = ea & ~3u, m = load_mem<uint32_t>(a, addr, w);
            const uint32_t keep = sh == 3 ? 0u : 0xffffffffu << ((sh + 1) * 8);
            store_mem<uint32_t>(a, (uint32_t(t) >> (24 - sh * 8)) | (m & keep), addr, w);
            return;
        }
        case 0x2e: {  // SWR
            const uint32_t sh = ea & 3, a = ea & ~3u, m = load_mem<uint32_t>(a, addr, w);
            const uint32_t keep = sh == 0 ? 0u : 0xffffffffu >> ((4 - sh) * 8);
            store_mem<uint32_t>(a, (uint32_t(t) << (sh * 8)) | (m & keep), addr, w);
            return;
        }
        case 0x2c: {  // SDL
            const uint32_t sh = ea & 7, a = ea & ~7u;
            const uint64_t m = load_mem<uint64_t>(a, addr, w);
            const uint64_t keep = sh == 7 ? 0ull : ~0ull << ((sh + 1) * 8);
            store_mem<uint64_t>(a, (t >> (56 - sh * 8)) | (m & keep), addr, w);
            return;
        }
        case 0x2d: {  // SDR
            const uint32_t sh = ea & 7, a = ea & ~7u;
            const uint64_t m = load_mem<uint64_t>(a, addr, w);
            const uint64_t keep = sh == 0 ? 0ull : ~0ull >> ((8 - sh) * 8);
            store_mem<uint64_t>(a, (t << (sh * 8)) | (m & keep), addr, w);
            return;
        }
        default: fail("instrução desconhecida", addr, w);
    }
}

void MipsMachine::special(uint32_t addr, uint32_t w, bool in_delay) {
    const int rs = int((w >> 21) & 31);
    const int rt = int((w >> 16) & 31);
    const int rd = int((w >> 11) & 31);
    const uint32_t sa = (w >> 6) & 31;
    const uint64_t s = gpr_[rs].lo, t = gpr_[rt].lo;
    uint64_t& d = gpr_[rd].lo;
    switch (w & 63) {
        case 0x00: d = sext32(uint32_t(t) << sa); return;                          // SLL (nop = sll 0,0,0)
        case 0x02: d = sext32(uint32_t(t) >> sa); return;                          // SRL
        case 0x03: d = sext32(uint32_t(int32_t(uint32_t(t)) >> sa)); return;       // SRA
        case 0x04: d = sext32(uint32_t(t) << (s & 31)); return;                    // SLLV
        case 0x06: d = sext32(uint32_t(t) >> (s & 31)); return;                    // SRLV
        case 0x07: d = sext32(uint32_t(int32_t(uint32_t(t)) >> (s & 31))); return; // SRAV
        case 0x08: branch(addr, w, in_delay, true, uint32_t(s), false); return;    // JR
        case 0x0a:                                                                 // MOVZ (64 bits)
            if (t == 0) d = s;
            return;
        case 0x0b:                                                                 // MOVN
            if (t != 0) d = s;
            return;
        case 0x0f: return;                                                         // SYNC
        case 0x09: {                                                               // JALR
            if (in_delay) fail("desvio dentro de um delay slot", addr, w);
            const uint32_t target = uint32_t(s);
            d = sext32(addr + 8);
            branch(addr, w, in_delay, true, target, false);
            return;
        }
        case 0x10: d = hi_; return;  // MFHI
        case 0x12: d = lo_; return;  // MFLO
        case 0x18: {                 // MULT (no R5900 também escreve rd = LO)
            const int64_t p = int64_t(int32_t(uint32_t(s))) * int64_t(int32_t(uint32_t(t)));
            lo_ = sext32(uint32_t(uint64_t(p)));
            hi_ = sext32(uint32_t(uint64_t(p) >> 32));
            d = lo_;
            return;
        }
        case 0x19: {  // MULTU
            const uint64_t p = uint64_t(uint32_t(s)) * uint64_t(uint32_t(t));
            lo_ = sext32(uint32_t(p));
            hi_ = sext32(uint32_t(p >> 32));
            d = lo_;
            return;
        }
        case 0x21: d = sext32(uint32_t(s) + uint32_t(t)); return;  // ADDU
        case 0x23: d = sext32(uint32_t(s) - uint32_t(t)); return;  // SUBU
        case 0x24: d = s & t; return;                              // AND
        case 0x25: d = s | t; return;                              // OR
        case 0x26: d = s ^ t; return;                              // XOR
        case 0x27: d = ~(s | t); return;                           // NOR
        case 0x2a: d = int64_t(s) < int64_t(t) ? 1 : 0; return;    // SLT
        case 0x2b: d = s < t ? 1 : 0; return;                      // SLTU
        case 0x2d: d = s + t; return;                              // DADDU
        case 0x2f: d = s - t; return;                              // DSUBU
        case 0x38: d = t << sa; return;                            // DSLL
        case 0x3a: d = t >> sa; return;                            // DSRL
        case 0x3b: d = uint64_t(int64_t(t) >> sa); return;         // DSRA
        case 0x3c: d = t << (sa + 32); return;                     // DSLL32
        case 0x3e: d = t >> (sa + 32); return;                     // DSRL32
        case 0x3f: d = uint64_t(int64_t(t) >> (sa + 32)); return;  // DSRA32
        default: fail("instrução desconhecida (SPECIAL)", addr, w);
    }
}

void MipsMachine::regimm(uint32_t addr, uint32_t w, bool in_delay) {
    const int64_t s = int64_t(gpr_[(w >> 21) & 31].lo);
    const uint32_t target = addr + 4 + uint32_t(sext16(w & 0xffff) << 2);
    switch ((w >> 16) & 31) {
        case 0x00: branch(addr, w, in_delay, s < 0, target, false); return;   // BLTZ
        case 0x01: branch(addr, w, in_delay, s >= 0, target, false); return;  // BGEZ
        case 0x02: branch(addr, w, in_delay, s < 0, target, true); return;    // BLTZL
        case 0x03: branch(addr, w, in_delay, s >= 0, target, true); return;   // BGEZL
        default: fail("instrução desconhecida (REGIMM)", addr, w);
    }
}

void MipsMachine::cop1(uint32_t addr, uint32_t w, bool in_delay) {
    const uint32_t fmt = (w >> 21) & 31;
    const int ft = int((w >> 16) & 31);
    const int fs = int((w >> 11) & 31);
    const int fd = int((w >> 6) & 31);
    switch (fmt) {
        case 0x00: set_gpr(ft, sext32(fpr_[fs])); return;   // MFC1 (rt no campo ft)
        case 0x04: fpr_[fs] = uint32_t(gpr_[ft].lo); return;  // MTC1
        case 0x08: {                                          // BC1F/BC1T/BC1FL/BC1TL
            const uint32_t kind = (w >> 16) & 31;
            if (kind > 3) break;
            const bool want = (kind & 1) != 0;
            const uint32_t target = addr + 4 + uint32_t(sext16(w & 0xffff) << 2);
            branch(addr, w, in_delay, fcc_ == want, target, (kind & 2) != 0);
            return;
        }
        case 0x10: {  // formato S
            const uint32_t a = fpr_[fs], b = fpr_[ft];
            switch (w & 63) {
                case 0x00: fpr_[fd] = add_zero(fin(a), fin(b)); return;   // ADD.S
                case 0x01: fpr_[fd] = add_zero(fin(a), -fin(b)); return;  // SUB.S
                case 0x02: fpr_[fd] = round_zero(fin(a) * fin(b)); return;  // MUL.S (produto exato em double)
                case 0x03:                                                  // DIV.S
                    if (fin(b) == 0.0) {
                        fpr_[fd] = ((a ^ b) & 0x80000000u) | 0x7f7fffffu;  // divisão por zero: ±FLT_MAX
                    } else {
                        fpr_[fd] = round_zero(fin(a) / fin(b));
                    }
                    return;
                case 0x04: {  // SQRT.S fd, ft: a raiz do módulo
                    const double v = fin(b & 0x7fffffffu);
                    fpr_[fd] = v == 0.0 ? 0u : round_zero(std::sqrt(v));
                    return;
                }
                case 0x16: {  // RSQRT.S fd, fs, ft: fs / sqrt(|ft|), a raiz truncada antes da divisão
                    const double v = fin(b & 0x7fffffffu);
                    const uint32_t r = v == 0.0 ? 0u : round_zero(std::sqrt(v));
                    if (fin(r) == 0.0) {
                        fpr_[fd] = (a & 0x80000000u) | 0x7f7fffffu;
                    } else {
                        fpr_[fd] = round_zero(fin(a) / fin(r));
                    }
                    return;
                }
                case 0x28:    // MAX.S: comparação de sinal e módulo, como a VU
                case 0x29: {  // MIN.S
                    auto key = [](uint32_t x) {
                        const int64_t m = x & 0x7fffffff;
                        return (x & 0x80000000u) ? -m - 1 : m;
                    };
                    const bool a_ge = key(a) >= key(b);
                    fpr_[fd] = ((w & 63) == 0x28) == a_ge ? a : b;
                    return;
                }
                case 0x05: fpr_[fd] = a & 0x7fffffffu; return;  // ABS.S
                case 0x06: fpr_[fd] = a; return;                // MOV.S
                case 0x07: fpr_[fd] = a ^ 0x80000000u; return;  // NEG.S
                case 0x24: {                                    // CVT.W.S: trunca e satura
                    if ((a & 0x7f800000u) <= 0x4e800000u) {
                        fpr_[fd] = uint32_t(int32_t(fin(a)));
                    } else {
                        fpr_[fd] = (a & 0x80000000u) ? 0x80000000u : 0x7fffffffu;
                    }
                    return;
                }
                case 0x30: fcc_ = false; return;                 // C.F.S
                case 0x32: fcc_ = fin(a) == fin(b); return;      // C.EQ.S
                case 0x34: fcc_ = fin(a) < fin(b); return;       // C.LT.S
                case 0x36: fcc_ = fin(a) <= fin(b); return;      // C.LE.S
                default: break;
            }
            break;
        }
        case 0x14:  // formato W
            if ((w & 63) == 0x20) {  // CVT.S.W
                fpr_[fd] = round_zero(double(int32_t(fpr_[fs])));
                return;
            }
            break;
        default: break;
    }
    fail("instrução desconhecida (COP1)", addr, w);
}

void MipsMachine::cop2(uint32_t addr, uint32_t w, bool in_delay) {
    if (!vu0_) fail("instrução COP2 sem VU0 ligada", addr, w);
    const uint32_t rs = (w >> 21) & 31;
    const int rt = int((w >> 16) & 31);
    const int rd = int((w >> 11) & 31);
    ps2::VuRegs& r = vu0_->regs();
    if (w & (1u << 25)) {  // CO: uma instrução em modo macro
        try {
            if (vu0_->macro(w)) return;
        } catch (const ps2::VuError& e) {
            fail(std::string("COP2 macro: ") + e.what(), addr, w);
        }
        // VCALLMS imm15 / VCALLMSR (CMSAR0).
        const uint32_t start = (w & 63) == 0x38 ? (w >> 6) & 0x7fff : cmsar0_;
        try {
            vu0_->run(start);
        } catch (const ps2::VuError& e) {
            fail(std::string("microprograma da VU0: ") + e.what(), addr, w);
        }
        ++vu0_calls_;
        return;
    }
    auto ctrl_read = [&](int n) -> uint32_t {
        if (n < 16) return r.vi[n];
        switch (n) {
            case 16: return r.status;
            case 17: return r.mac;
            case 18: return r.clip;
            case 20: return r.r;
            case 21: return r.i;
            case 22: return r.q;
            case 27: return cmsar0_;
            default: return 0;
        }
    };
    switch (rs) {
        case 1: {  // QMFC2 rt, vf[rd]
            const uint32_t* v = r.vf[rd];
            if (rt != 0)
                gpr_[rt] = {uint64_t(v[0]) | (uint64_t(v[1]) << 32), uint64_t(v[2]) | (uint64_t(v[3]) << 32)};
            return;
        }
        case 2: set_gpr(rt, sext32(ctrl_read(rd))); return;  // CFC2
        case 5: {  // QMTC2 rt, vf[rd]
            if (rd == 0) return;
            const Gpr g = gpr_[rt];
            r.vf[rd][0] = uint32_t(g.lo);
            r.vf[rd][1] = uint32_t(g.lo >> 32);
            r.vf[rd][2] = uint32_t(g.hi);
            r.vf[rd][3] = uint32_t(g.hi >> 32);
            return;
        }
        case 6: {  // CTC2 rt, ctrl[rd]
            const uint32_t v = uint32_t(gpr_[rt].lo);
            if (rd > 0 && rd < 16) r.vi[rd] = uint16_t(v);
            else if (rd == 16) r.status = v & 0xfff;
            else if (rd == 17) r.mac = v & 0xffff;
            else if (rd == 18) r.clip = v & 0xffffff;
            else if (rd == 20) r.r = v;
            else if (rd == 21) r.i = v;
            else if (rd == 22) r.q = v;
            else if (rd == 27) cmsar0_ = v & 0xffff;
            return;
        }
        case 8: {  // BC2F/BC2T/BC2FL/BC2TL: a VU0 nunca está ocupada aqui
            const uint32_t kind = (w >> 16) & 31;
            const bool busy = false;
            const uint32_t target = addr + 4 + uint32_t(sext16(w & 0xffff) << 2);
            branch(addr, w, in_delay, busy == ((kind & 1) != 0), target, (kind & 2) != 0);
            return;
        }
        default: break;
    }
    fail("instrução desconhecida (COP2)", addr, w);
}

void MipsMachine::mmi(uint32_t addr, uint32_t w) {
    const int rs = int((w >> 21) & 31);
    const int rt = int((w >> 16) & 31);
    const int rd = int((w >> 11) & 31);
    const uint32_t sa = (w >> 6) & 31;
    const Gpr a = gpr_[rs], b = gpr_[rt];
    auto word = [](const Gpr& g, int i) {
        return uint32_t(i < 2 ? g.lo >> (32 * i) : g.hi >> (32 * (i - 2)));
    };
    auto make = [](uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
        return Gpr{uint64_t(w0) | (uint64_t(w1) << 32), uint64_t(w2) | (uint64_t(w3) << 32)};
    };
    Gpr out{};
    switch (w & 63) {
        case 0x08:  // MMI0
            if (sa == 0x12) {  // PEXTLW
                out = make(word(b, 0), word(a, 0), word(b, 1), word(a, 1));
                break;
            }
            fail("instrução desconhecida (MMI0)", addr, w);
        case 0x28:  // MMI1
            if (sa == 0x12) {  // PEXTUW
                out = make(word(b, 2), word(a, 2), word(b, 3), word(a, 3));
                break;
            }
            fail("instrução desconhecida (MMI1)", addr, w);
        case 0x09:  // MMI2
            if (sa == 0x0e) {  // PCPYLD
                out = {b.lo, a.lo};
                break;
            }
            fail("instrução desconhecida (MMI2)", addr, w);
        case 0x29:  // MMI3
            if (sa == 0x0e) {  // PCPYUD
                out = {a.hi, b.hi};
                break;
            }
            if (sa == 0x12) {  // POR
                out = {a.lo | b.lo, a.hi | b.hi};
                break;
            }
            if (sa == 0x13) {  // PNOR
                out = {~(a.lo | b.lo), ~(a.hi | b.hi)};
                break;
            }
            fail("instrução desconhecida (MMI3)", addr, w);
        default: fail("instrução desconhecida (MMI)", addr, w);
    }
    if (rd != 0) gpr_[rd] = out;
}

}  // namespace nf
