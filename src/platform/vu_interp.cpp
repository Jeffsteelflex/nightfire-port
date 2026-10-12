// Interpretador das VUs do PS2 (platform/vu_interp.h).

#include "platform/vu_interp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "platform/ps2_float.h"

namespace ps2 {

namespace {

constexpr uint32_t kFmacLatency = 4;
constexpr uint32_t kDivLatency = 7, kSqrtLatency = 7, kRsqrtLatency = 13;

inline bool lane_on(uint32_t mask, int lane) { return (mask >> (3 - lane)) & 1; }
inline int32_t sext(uint32_t v, int bits) {
    const uint32_t m = 1u << (bits - 1);
    v &= (1u << bits) - 1;
    return int32_t((v ^ m) - m);
}

// Bits do MAC flag de uma pista (x = 3, y = 2, z = 1, w = 0).
inline uint32_t mac_bits(const FloatFlags& f, int lane) {
    const int s = 3 - lane;
    return (f.zero ? 1u << s : 0) | (f.sign ? 0x10u << s : 0) | (f.underflow ? 0x100u << s : 0) |
           (f.overflow ? 0x1000u << s : 0);
}

}  // namespace

VuMachine::VuMachine(VuUnit unit)
    : unit_(unit),
      micro_(unit == VuUnit::VU0 ? 4096 : 16384, 0),
      data_(unit == VuUnit::VU0 ? 4096 : 16384, 0),
      micro_mask_(unit == VuUnit::VU0 ? 0x1ff : 0x7ff),
      data_mask_(unit == VuUnit::VU0 ? 0xff : 0x3ff) {
    r_.vf[0][3] = 0x3f800000;  // vf00 = (0, 0, 0, 1)
}

uint32_t* VuMachine::qword(uint32_t n) {
    if (n > data_mask_) ++stats_.address_wraps;
    return reinterpret_cast<uint32_t*>(data_.data() + 16 * (n & data_mask_));
}

void VuMachine::load_micro(uint32_t instr, const void* code, uint32_t bytes) {
    const uint8_t* src = static_cast<const uint8_t*>(code);
    for (uint32_t i = 0; i < bytes; ++i) micro_[(instr * 8 + i) % micro_.size()] = src[i];
}

void VuMachine::fail(const char* what, uint32_t pc, uint32_t lo, uint32_t up) const {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "VU%d: %s em 0x%03x (%08x %08x)", unit_ == VuUnit::VU0 ? 0 : 1, what, pc, up,
                  lo);
    throw VuError(buf, pc, lo, up);
}

// ---------------------------------------------------------------------------------------------------- de cima

VuMachine::UpperResult VuMachine::upper(uint32_t w) const {
    UpperResult u;
    const uint32_t op = w & 0x3f;
    const uint32_t dest = vu_dest(w);
    const int ft = (w >> 16) & 31, fs = (w >> 11) & 31, fd = (w >> 6) & 31;
    const uint32_t* s = r_.vf[fs];
    const uint32_t* t = r_.vf[ft];
    u.mask = dest;

    enum Kind { kAdd, kSub, kMul, kMadd, kMsub, kMax, kMin, kOpmula, kOpmsub, kNone };
    Kind kind = kNone;
    int src2 = 0;  // 0: ft inteiro; 1: ft[bc]; 2: Q; 3: I
    int bc = 0;
    bool acc_dest = false;

    if ((op & 0x3c) != 0x3c) {
        u.reg = fd;
        if (op < 0x1c) {
            static const Kind k[7] = {kAdd, kSub, kMadd, kMsub, kMax, kMin, kMul};
            kind = k[op >> 2];
            src2 = 1;
            bc = op & 3;
        } else if (op < 0x28) {
            static const Kind k[12] = {kMul, kMax, kMul, kMin, kAdd, kMadd, kAdd, kMadd, kSub, kMsub, kSub, kMsub};
            static const int r[12] = {2, 3, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3};
            kind = k[op - 0x1c];
            src2 = r[op - 0x1c];
        } else if (op < 0x30) {
            static const Kind k[8] = {kAdd, kMadd, kMul, kMax, kSub, kMsub, kOpmsub, kMin};
            kind = k[op - 0x28];
            src2 = 0;
        } else {
            u.reg = -2;  // 0x30-0x3b: só existem no modo macro (VIADD, VCALLMS...)
            return u;
        }
    } else {
        const uint32_t sp = (((w >> 6) & 0x1f) << 2) | (w & 3);
        if (sp == 0x2f) return u;  // NOP
        if (sp >= 0x10 && sp <= 0x17) {  // ITOF0/4/12/15, FTOI0/4/12/15: ft <- fs
            static const int frac[4] = {0, 4, 12, 15};
            u.reg = ft;
            for (int l = 0; l < 4; ++l) {
                if (!lane_on(dest, l)) continue;
                u.value[l] = sp < 0x14 ? from_int(int32_t(s[l]), frac[sp & 3])
                                       : uint32_t(to_int(s[l], frac[sp & 3]));
            }
            return u;
        }
        if (sp == 0x1d) {  // ABS ft, fs
            u.reg = ft;
            for (int l = 0; l < 4; ++l) u.value[l] = abs(s[l]);
            return u;
        }
        if (sp == 0x1f) {  // CLIPw.xyz fs, ft: seis comparações com |ft.w|
            const uint32_t wv = abs(t[3]);
            uint32_t bits = 0;
            for (int l = 0; l < 3; ++l) {
                if (less(wv, s[l])) bits |= 1u << (2 * l);        // +x, +y, +z
                if (less(s[l], neg(wv))) bits |= 2u << (2 * l);   // -x, -y, -z
            }
            u.sets_clip = true;
            u.mac = bits;  // usado como resultado do CLIP em commit_upper
            u.mask = 0;
            return u;
        }
        acc_dest = true;
        u.to_acc = true;
        if (sp < 0x10) {
            static const Kind k[4] = {kAdd, kSub, kMadd, kMsub};
            kind = k[sp >> 2];
            src2 = 1;
            bc = sp & 3;
        } else if (sp >= 0x18 && sp <= 0x1b) {
            kind = kMul;
            src2 = 1;
            bc = sp & 3;
        } else if (sp == 0x1c || sp == 0x1e) {
            kind = kMul;
            src2 = sp == 0x1c ? 2 : 3;
        } else if (sp >= 0x20 && sp <= 0x27) {
            static const Kind k[8] = {kAdd, kMadd, kAdd, kMadd, kSub, kMsub, kSub, kMsub};
            kind = k[sp - 0x20];
            src2 = (sp & 2) ? 3 : 2;
        } else if (sp >= 0x28 && sp <= 0x2e && sp != 0x2b) {
            static const Kind k[7] = {kAdd, kMadd, kMul, kNone, kSub, kMsub, kOpmula};
            kind = k[sp - 0x28];
            src2 = 0;
        } else {
            u.reg = -2;  // desconhecida
            return u;
        }
    }

    // Segundo operando de cada pista.
    uint32_t b[4];
    for (int l = 0; l < 4; ++l) {
        switch (src2) {
        case 0: b[l] = t[l]; break;
        case 1: b[l] = t[bc]; break;
        case 2: b[l] = r_.q; break;
        default: b[l] = r_.i; break;
        }
    }
    if (kind == kOpmula || kind == kOpmsub) {  // produto vetorial: fs.yzx * ft.zxy (só xyz)
        static const int i1[3] = {1, 2, 0}, i2[3] = {2, 0, 1};
        for (int l = 0; l < 3; ++l) b[l] = t[i2[l]];
        uint32_t a[3];
        for (int l = 0; l < 3; ++l) a[l] = s[i1[l]];
        u.sets_mac = true;
        u.mask &= 0xe;
        for (int l = 0; l < 3; ++l) {
            if (!lane_on(dest, l)) continue;
            FloatFlags f;
            u.value[l] = kind == kOpmula ? mul(a[l], b[l], &f) : msub(r_.acc[l], a[l], b[l], &f);
            u.mac |= mac_bits(f, l);
        }
        if (kind == kOpmula) u.to_acc = true;
        (void)acc_dest;
        return u;
    }
    if (kind == kNone) {
        u.reg = -2;
        return u;
    }
    u.sets_mac = kind != kMax && kind != kMin;
    for (int l = 0; l < 4; ++l) {
        if (!lane_on(dest, l)) continue;
        FloatFlags f;
        switch (kind) {
        case kAdd: u.value[l] = add(s[l], b[l], &f); break;
        case kSub: u.value[l] = sub(s[l], b[l], &f); break;
        case kMul: u.value[l] = mul(s[l], b[l], &f); break;
        case kMadd: u.value[l] = madd(r_.acc[l], s[l], b[l], &f); break;
        case kMsub: u.value[l] = msub(r_.acc[l], s[l], b[l], &f); break;
        case kMax: u.value[l] = max(s[l], b[l]); break;
        case kMin: u.value[l] = min(s[l], b[l]); break;
        default: break;
        }
        if (u.sets_mac) u.mac |= mac_bits(f, l);
    }
    return u;
}

void VuMachine::commit_upper(const UpperResult& u) {
    if (u.sets_clip) {
        clip_now_ = ((clip_now_ << 6) | u.mac) & 0xffffff;
        return;
    }
    uint32_t* dst = u.to_acc ? r_.acc : (u.reg > 0 ? r_.vf[u.reg] : nullptr);
    if (dst) {
        for (int l = 0; l < 4; ++l)
            if (lane_on(u.mask, l)) dst[l] = u.value[l];
    }
    if (u.sets_mac) {
        mac_now_ = u.mac;
        const uint32_t zsuo = ((u.mac & 0x000f) ? 1u : 0) | ((u.mac & 0x00f0) ? 2u : 0) | ((u.mac & 0x0f00) ? 4u : 0) |
                              ((u.mac & 0xf000) ? 8u : 0);
        status_now_ = (status_now_ & 0xff0) | zsuo | (zsuo << 6);
    }
}

// ---------------------------------------------------------------------------------------------------- de baixo

void VuMachine::backup_vi(int reg) {
    // O "VI backup" do PCSX2: o desvio condicional logo depois lê o valor de antes desta escrita.
    if (vi_backup_cycles_ && reg == vi_backup_reg_) {
        vi_backup_cycles_ = 2;
        return;
    }
    vi_backup_cycles_ = 2;
    vi_backup_reg_ = reg;
    vi_backup_value_ = r_.vi[reg & 15];
}

uint16_t VuMachine::read_vi_for_branch(int reg) const {
    if (vi_backup_cycles_ > 0 && reg == vi_backup_reg_) return vi_backup_value_;
    return r_.vi[reg & 15];
}

void VuMachine::set_vi(int reg, uint16_t v) {
    if ((reg & 15) != 0) r_.vi[reg & 15] = v;
}

uint32_t VuMachine::data_word(uint32_t qw, int field) { return qword(qw)[field]; }

bool VuMachine::lower_writes_vf(uint32_t w, int& reg) const {
    const uint32_t op = w >> 25;
    const int it = (w >> 16) & 31;
    reg = -1;
    if (op == 0x00) {  // LQ
        reg = it;
        return true;
    }
    if (op != 0x40 || (w & 0x3c) != 0x3c) return false;
    const uint32_t sp = (((w >> 6) & 0x1f) << 2) | (w & 3);
    switch (sp) {
    case 0x30: case 0x31: case 0x34: case 0x36: case 0x3d: case 0x40: case 0x41: case 0x64:
        if (sp == 0x30 && vu_dest(w) == 0 && it == 0 && ((w >> 11) & 31) == 0) return false;  // NOP
        reg = it;
        return true;
    default:
        return false;
    }
}

void VuMachine::lower_special(uint32_t w) {
    const uint32_t sp = (((w >> 6) & 0x1f) << 2) | (w & 3);
    const uint32_t dest = vu_dest(w);
    const int ft = (w >> 16) & 31, fs = (w >> 11) & 31;
    const int fsf = (w >> 21) & 3, ftf = (w >> 23) & 3;
    uint32_t* t = r_.vf[ft];
    const uint32_t* s = r_.vf[fs];
    auto write_vf = [&](int reg, const uint32_t v[4]) {
        if (reg == 0) return;
        for (int l = 0; l < 4; ++l)
            if (lane_on(dest, l)) r_.vf[reg][l] = v[l];
    };
    auto start_fdiv = [&](uint32_t value, uint32_t latency, const DivFlags& f) {
        fdiv_.active = true;
        fdiv_.start = cycle_;
        fdiv_.latency = latency;
        fdiv_.value = value;
        fdiv_.status = (f.invalid ? 0x10u : 0) | (f.divzero ? 0x20u : 0);
    };
    auto start_efu = [&](uint32_t value, uint32_t latency) {
        efu_.active = true;
        efu_.start = cycle_;
        efu_.latency = latency;
        efu_.value = value;
    };
    auto advance_r = [&]() {
        const uint32_t x = (r_.r >> 4) & 1, y = (r_.r >> 22) & 1;
        r_.r = (((r_.r << 1) ^ x ^ y) & 0x7fffff) | 0x3f800000;
    };
    switch (sp) {
    case 0x30: {  // MOVE
        uint32_t v[4] = {s[0], s[1], s[2], s[3]};
        write_vf(ft, v);
        return;
    }
    case 0x31: {  // MR32: x <- y, y <- z, z <- w, w <- x
        uint32_t v[4] = {s[1], s[2], s[3], s[0]};
        write_vf(ft, v);
        return;
    }
    case 0x34: {  // LQI ft, (is++)
        const uint32_t* m = qword(r_.vi[fs & 15]);
        uint32_t v[4] = {m[0], m[1], m[2], m[3]};
        write_vf(ft, v);
        backup_vi(fs);
        set_vi(fs, uint16_t(r_.vi[fs & 15] + 1));
        return;
    }
    case 0x35: {  // SQI fs, (it++)
        uint32_t* m = qword(r_.vi[ft & 15]);
        for (int l = 0; l < 4; ++l)
            if (lane_on(dest, l)) m[l] = s[l];
        backup_vi(ft);
        set_vi(ft, uint16_t(r_.vi[ft & 15] + 1));
        return;
    }
    case 0x36: {  // LQD ft, (--is)
        backup_vi(fs);
        set_vi(fs, uint16_t(r_.vi[fs & 15] - 1));
        const uint32_t* m = qword(r_.vi[fs & 15]);
        uint32_t v[4] = {m[0], m[1], m[2], m[3]};
        write_vf(ft, v);
        return;
    }
    case 0x37: {  // SQD fs, (--it)
        backup_vi(ft);
        set_vi(ft, uint16_t(r_.vi[ft & 15] - 1));
        uint32_t* m = qword(r_.vi[ft & 15]);
        for (int l = 0; l < 4; ++l)
            if (lane_on(dest, l)) m[l] = s[l];
        return;
    }
    case 0x38: {  // DIV Q, fs.fsf, ft.ftf
        DivFlags f;
        const uint32_t q = div(s[fsf], t[ftf], &f);
        start_fdiv(q, kDivLatency, f);
        return;
    }
    case 0x39: {  // SQRT Q, ft.ftf
        DivFlags f;
        const uint32_t q = ps2::sqrt(t[ftf], &f);
        start_fdiv(q, kSqrtLatency, f);
        return;
    }
    case 0x3a: {  // RSQRT Q, fs.fsf, ft.ftf
        DivFlags f;
        const uint32_t q = rsqrt(s[fsf], t[ftf], &f);
        start_fdiv(q, kRsqrtLatency, f);
        return;
    }
    case 0x3b: return;  // WAITQ (a espera foi feita antes)
    case 0x3c:          // MTIR it, fs.fsf
        backup_vi(ft);
        set_vi(ft, uint16_t(s[fsf]));
        return;
    case 0x3d: {  // MFIR.dest ft, is
        const uint32_t v1 = uint32_t(int32_t(int16_t(r_.vi[fs & 15])));
        uint32_t v[4] = {v1, v1, v1, v1};
        write_vf(ft, v);
        return;
    }
    case 0x3e: {  // ILWR.dest it, (is)
        for (int l = 0; l < 4; ++l)
            if (lane_on(dest, l)) {
                set_vi(ft, uint16_t(data_word(r_.vi[fs & 15], l)));
                break;
            }
        return;
    }
    case 0x3f: {  // ISWR.dest it, (is)
        uint32_t* m = qword(r_.vi[fs & 15]);
        for (int l = 0; l < 4; ++l)
            if (lane_on(dest, l)) m[l] = r_.vi[ft & 15];
        return;
    }
    case 0x40: {  // RNEXT
        advance_r();
        uint32_t v[4] = {r_.r, r_.r, r_.r, r_.r};
        write_vf(ft, v);
        return;
    }
    case 0x41: {  // RGET
        uint32_t v[4] = {r_.r, r_.r, r_.r, r_.r};
        write_vf(ft, v);
        return;
    }
    case 0x42: r_.r = 0x3f800000 | (s[fsf] & 0x7fffff); return;               // RINIT
    case 0x43: r_.r = 0x3f800000 | ((r_.r ^ s[fsf]) & 0x7fffff); return;      // RXOR
    case 0x64: {  // MFP
        uint32_t v[4] = {r_.p, r_.p, r_.p, r_.p};
        write_vf(ft, v);
        return;
    }
    case 0x68: set_vi(ft, top_); return;    // XTOP
    case 0x69: set_vi(ft, itop_); return;   // XITOP
    case 0x70: {  // ESADD P = x² + y² + z²
        start_efu(add(add(mul(s[0], s[0]), mul(s[1], s[1])), mul(s[2], s[2])), 11);
        return;
    }
    case 0x71: {  // ERSADD P = 1 / (x² + y² + z²)
        start_efu(div(0x3f800000, add(add(mul(s[0], s[0]), mul(s[1], s[1])), mul(s[2], s[2]))), 18);
        return;
    }
    case 0x72: {  // ELENG
        start_efu(ps2::sqrt(add(add(mul(s[0], s[0]), mul(s[1], s[1])), mul(s[2], s[2]))), 18);
        return;
    }
    case 0x73: {  // ERLENG
        start_efu(rsqrt(0x3f800000, add(add(mul(s[0], s[0]), mul(s[1], s[1])), mul(s[2], s[2]))), 24);
        return;
    }
    case 0x74: start_efu(from_double(std::atan2(value(s[1]), value(s[0]))), 54); return;  // EATANxy
    case 0x75: start_efu(from_double(std::atan2(value(s[2]), value(s[0]))), 54); return;  // EATANxz
    case 0x76: start_efu(add(add(add(s[0], s[1]), s[2]), s[3]), 12); return;               // ESUM
    case 0x78: start_efu(ps2::sqrt(s[fsf]), 12); return;                                  // ESQRT
    case 0x79: start_efu(rsqrt(0x3f800000, s[fsf]), 18); return;                          // ERSQRT
    case 0x7a: start_efu(div(0x3f800000, s[fsf]), 12); return;                            // ERCPR
    case 0x7b: return;                                                                     // WAITP
    case 0x7c: start_efu(efu_sin(s[fsf]), 29); return;                                    // ESIN
    case 0x7d: start_efu(efu_atan(s[fsf]), 54); return;                                   // EATAN
    case 0x7e: start_efu(efu_exp(s[fsf]), 44); return;                                    // EEXP
    default: break;
    }
    throw VuError("instrução de baixo desconhecida", 0, w, 0);
}

void VuMachine::lower(uint32_t w, uint32_t pc, int64_t& branch_target) {
    const uint32_t op = w >> 25;
    const uint32_t dest = vu_dest(w);
    const int it = (w >> 16) & 31, is = (w >> 11) & 31, id = (w >> 6) & 31;
    const int32_t imm11 = sext(w & 0x7ff, 11);
    branch_target = -1;
    switch (op) {
    case 0x00: {  // LQ.dest ft, imm(is)
        const uint32_t* m = qword(uint32_t(r_.vi[is & 15] + imm11));
        if (it != 0)
            for (int l = 0; l < 4; ++l)
                if (lane_on(dest, l)) r_.vf[it][l] = m[l];
        return;
    }
    case 0x01: {  // SQ.dest fs, imm(it)
        uint32_t* m = qword(uint32_t(r_.vi[it & 15] + imm11));
        for (int l = 0; l < 4; ++l)
            if (lane_on(dest, l)) m[l] = r_.vf[is][l];
        return;
    }
    case 0x04: {  // ILW.dest it, imm(is)
        const uint32_t qw = uint32_t(r_.vi[is & 15] + imm11);
        for (int l = 0; l < 4; ++l)
            if (lane_on(dest, l)) {
                set_vi(it, uint16_t(data_word(qw, l)));
                break;
            }
        return;
    }
    case 0x05: {  // ISW.dest it, imm(is)
        uint32_t* m = qword(uint32_t(r_.vi[is & 15] + imm11));
        for (int l = 0; l < 4; ++l)
            if (lane_on(dest, l)) m[l] = r_.vi[it & 15];
        return;
    }
    case 0x08: case 0x09: {  // IADDIU / ISUBIU it, is, imm15
        const uint16_t imm15 = uint16_t(((w >> 10) & 0x7800) | (w & 0x7ff));
        backup_vi(it);
        set_vi(it, uint16_t(op == 0x08 ? r_.vi[is & 15] + imm15 : r_.vi[is & 15] - imm15));
        return;
    }
    case 0x10: set_vi(1, (r_.clip & 0xffffff) == (w & 0xffffff) ? 1 : 0); return;          // FCEQ
    case 0x11: clip_now_ = r_.clip = w & 0xffffff; return;                                   // FCSET
    case 0x12: set_vi(1, (r_.clip & w & 0xffffff) ? 1 : 0); return;                          // FCAND
    case 0x13: set_vi(1, ((r_.clip | w) & 0xffffff) == 0xffffff ? 1 : 0); return;            // FCOR
    case 0x14: case 0x15: case 0x16: case 0x17: {
        const uint32_t imm12 = ((w >> 10) & 0x800) | (w & 0x7ff);
        if (op == 0x14) set_vi(it, (r_.status & 0xfff) == imm12 ? 1 : 0);   // FSEQ
        else if (op == 0x16) set_vi(it, uint16_t(r_.status & imm12));      // FSAND
        else if (op == 0x17) set_vi(it, uint16_t((r_.status | imm12) & 0xfff));  // FSOR
        else {                                                               // FSSET: os persistentes
            status_now_ = (status_now_ & 0x3f) | (imm12 & 0xfc0);
            r_.status = (r_.status & 0x3f) | (imm12 & 0xfc0);
        }
        return;
    }
    case 0x18: set_vi(it, (r_.mac & 0xffff) == r_.vi[is & 15] ? 1 : 0); return;  // FMEQ
    case 0x1a: set_vi(it, uint16_t(r_.vi[is & 15] & r_.mac)); return;            // FMAND
    case 0x1b: set_vi(it, uint16_t(r_.vi[is & 15] | r_.mac)); return;            // FMOR
    case 0x1c: set_vi(it, uint16_t(r_.clip & 0xfff)); return;                    // FCGET
    case 0x20: branch_target = (pc + 1 + imm11) & micro_mask_; return;           // B
    case 0x21:                                                                   // BAL
        set_vi(it, uint16_t(pc + 2));
        branch_target = (pc + 1 + imm11) & micro_mask_;
        return;
    case 0x24: branch_target = r_.vi[is & 15] & micro_mask_; return;             // JR
    case 0x25: {                                                                 // JALR
        const uint16_t target = r_.vi[is & 15];
        set_vi(it, uint16_t(pc + 2));
        branch_target = target & micro_mask_;
        return;
    }
    case 0x28: case 0x29: {  // IBEQ / IBNE it, is
        const int16_t a = int16_t(read_vi_for_branch(it)), b = int16_t(read_vi_for_branch(is));
        if ((op == 0x28) == (a == b)) branch_target = (pc + 1 + imm11) & micro_mask_;
        return;
    }
    case 0x2c: case 0x2d: case 0x2e: case 0x2f: {  // IBLTZ / IBGTZ / IBLEZ / IBGEZ is
        const int16_t a = int16_t(read_vi_for_branch(is));
        const bool taken = op == 0x2c ? a < 0 : op == 0x2d ? a > 0 : op == 0x2e ? a <= 0 : a >= 0;
        if (taken) branch_target = (pc + 1 + imm11) & micro_mask_;
        return;
    }
    case 0x40: {
        const uint32_t fn = w & 0x3f;
        switch (fn) {
        case 0x30: backup_vi(id); set_vi(id, uint16_t(r_.vi[is & 15] + r_.vi[it & 15])); return;  // IADD
        case 0x31: backup_vi(id); set_vi(id, uint16_t(r_.vi[is & 15] - r_.vi[it & 15])); return;  // ISUB
        case 0x32: backup_vi(it); set_vi(it, uint16_t(r_.vi[is & 15] + sext(uint32_t(id), 5))); return;  // IADDI
        case 0x34: backup_vi(id); set_vi(id, uint16_t(r_.vi[is & 15] & r_.vi[it & 15])); return;  // IAND
        case 0x35: backup_vi(id); set_vi(id, uint16_t(r_.vi[is & 15] | r_.vi[it & 15])); return;  // IOR
        default: break;
        }
        if ((fn & 0x3c) == 0x3c) {
            const uint32_t sp = (((w >> 6) & 0x1f) << 2) | (w & 3);
            if (sp == 0x6c) {  // XGKICK is
                if (unit_ != VuUnit::VU1) throw VuError("XGKICK na VU0", pc, w, 0);
                xgkick(pc, r_.vi[is & 15]);
                return;
            }
            lower_special(w);
            return;
        }
        break;
    }
    default: break;
    }
    throw VuError("instrução de baixo desconhecida", pc, w, 0);
}

void VuMachine::xgkick(uint32_t pc, uint32_t address) {
    GifKick k;
    k.pc = pc;
    k.address = address;
    uint32_t qw = address;
    for (int guard = 0; guard < 4096; ++guard) {
        const uint32_t* tag = qword(qw);
        auto push = [&](uint32_t n) {
            const uint32_t* q = qword(n);
            const uint8_t* b = reinterpret_cast<const uint8_t*>(q);
            k.data.insert(k.data.end(), b, b + 16);
        };
        push(qw++);
        const uint64_t lo = uint64_t(tag[0]) | (uint64_t(tag[1]) << 32);
        const uint64_t hi = uint64_t(tag[2]) | (uint64_t(tag[3]) << 32);
        const uint32_t nloop = uint32_t(lo & 0x7fff);
        const bool eop = (lo >> 15) & 1;
        const uint32_t flg = uint32_t((lo >> 58) & 3);
        uint32_t nreg = uint32_t((lo >> 60) & 0xf);
        if (nreg == 0) nreg = 16;
        (void)hi;
        uint32_t qwords = 0;
        if (flg == 0) qwords = nloop * nreg;                 // PACKED
        else if (flg == 1) qwords = (nloop * nreg + 1) / 2;  // REGLIST: dwords
        else qwords = nloop;                                  // IMAGE
        for (uint32_t n = 0; n < qwords; ++n) push(qw++);
        if (eop) break;
    }
    kicks_.push_back(std::move(k));
}

// ---------------------------------------------------------------------------------------------------- pipeline

void VuMachine::flush_pipes() {
    size_t n = 0;
    while (n < fmac_.size() && cycle_ - fmac_[n].start >= fmac_[n].latency) {
        r_.mac = fmac_[n].mac;
        r_.status = (r_.status & 0x30) | (fmac_[n].status & ~0x30u);
        r_.clip = fmac_[n].clip;
        ++n;
    }
    if (n) fmac_.erase(fmac_.begin(), fmac_.begin() + std::ptrdiff_t(n));
    if (fdiv_.active && cycle_ - fdiv_.start >= fdiv_.latency) {
        r_.q = fdiv_.value;
        // I e D: os bits 4-5 e os persistentes 10-11.
        r_.status = (r_.status & ~0x30u) | fdiv_.status | (fdiv_.status << 6);
        status_now_ = (status_now_ & ~0x30u) | fdiv_.status | (fdiv_.status << 6);
        fdiv_.active = false;
    }
    if (efu_.active && cycle_ - efu_.start >= efu_.latency) {
        r_.p = efu_.value;
        efu_.active = false;
    }
}

void VuMachine::flush_all() {
    for (const Pending& p : fmac_) {
        r_.mac = p.mac;
        r_.status = (r_.status & 0x30) | (p.status & ~0x30u);
        r_.clip = p.clip;
    }
    fmac_.clear();
    if (fdiv_.active) {
        r_.q = fdiv_.value;
        r_.status = (r_.status & ~0x30u) | fdiv_.status | (fdiv_.status << 6);
        status_now_ = (status_now_ & ~0x30u) | fdiv_.status | (fdiv_.status << 6);
        fdiv_.active = false;
    }
    if (efu_.active) {
        r_.p = efu_.value;
        efu_.active = false;
    }
}

namespace {

// Os VF que uma instrução de cima lê, com o campo de cada um (para a espera do pipeline).
struct Reads {
    int reg[3] = {-1, -1, -1};
    uint32_t mask[3] = {};
};

Reads upper_reads(uint32_t w) {
    Reads r;
    const uint32_t op = w & 0x3f;
    const uint32_t dest = vu_dest(w);
    const int ft = (w >> 16) & 31, fs = (w >> 11) & 31;
    if ((op & 0x3c) == 0x3c) {
        const uint32_t sp = (((w >> 6) & 0x1f) << 2) | (w & 3);
        if (sp == 0x2f) return r;
        r.reg[0] = fs;
        r.mask[0] = dest;
        if (sp == 0x1f) {
            r.mask[0] = 0xe;
            r.reg[1] = ft;
            r.mask[1] = 1;
            return r;
        }
        if (sp < 0x10 || (sp >= 0x18 && sp <= 0x1b)) {
            r.reg[1] = ft;
            r.mask[1] = 8u >> (sp & 3);
        } else if (sp >= 0x28 && sp <= 0x2e) {
            r.reg[1] = ft;
            r.mask[1] = dest;
            if (sp == 0x2e) r.mask[0] = r.mask[1] = 0xe;
        }
        return r;
    }
    r.reg[0] = fs;
    r.mask[0] = dest;
    if (op < 0x1c) {
        r.reg[1] = ft;
        r.mask[1] = 8u >> (op & 3);
    } else if (op >= 0x28) {
        r.reg[1] = ft;
        r.mask[1] = op == 0x2e ? 0xe : dest;
        if (op == 0x2e) r.mask[0] = 0xe;
    }
    return r;
}

Reads lower_reads(uint32_t w) {
    Reads r;
    const uint32_t op = w >> 25;
    const int ft = (w >> 16) & 31, fs = (w >> 11) & 31;
    const uint32_t fsf = (w >> 21) & 3, ftf = (w >> 23) & 3;
    if (op == 0x01) {  // SQ
        r.reg[0] = fs;
        r.mask[0] = vu_dest(w);
        return r;
    }
    if (op != 0x40 || (w & 0x3c) != 0x3c) return r;
    const uint32_t sp = (((w >> 6) & 0x1f) << 2) | (w & 3);
    switch (sp) {
    case 0x30: case 0x31: case 0x35: case 0x37:
        r.reg[0] = fs;
        r.mask[0] = sp == 0x31 ? 0xf : vu_dest(w);
        break;
    case 0x38: case 0x3a:
        r.reg[0] = fs;
        r.mask[0] = 8u >> fsf;
        r.reg[1] = ft;
        r.mask[1] = 8u >> ftf;
        break;
    case 0x39:
        r.reg[0] = ft;
        r.mask[0] = 8u >> ftf;
        break;
    case 0x3c: case 0x42: case 0x43: case 0x78: case 0x79: case 0x7a: case 0x7c: case 0x7d: case 0x7e:
        r.reg[0] = fs;
        r.mask[0] = 8u >> fsf;
        break;
    case 0x70: case 0x71: case 0x72: case 0x73:
        r.reg[0] = fs;
        r.mask[0] = 0xe;
        break;
    case 0x74: r.reg[0] = fs; r.mask[0] = 0xc; break;
    case 0x75: r.reg[0] = fs; r.mask[0] = 0xa; break;
    case 0x76: r.reg[0] = fs; r.mask[0] = 0xf; break;
    default: break;
    }
    return r;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------- execução

void VuMachine::run(uint32_t start, uint64_t max_steps) {
    uint32_t pc = start & micro_mask_;
    int ebit = 0;       // 2 no par com o bit E; o programa acaba depois do delay slot
    int branch = 0;     // 2 depois do desvio; no delay slot vira 1 e depois salta
    uint32_t branch_pc = 0;
    int delay_branch = 0;  // um desvio no delay slot de outro
    uint32_t delay_branch_pc = 0;
    // A disponibilidade de cada pista dos VF (ciclo em que o resultado sai do pipeline).
    uint64_t ready[32][4] = {};
    ++stats_.programs;

    for (uint64_t steps = 0;; ++steps) {
        if (steps >= max_steps) fail("passou do limite de instruções", pc, 0, 0);
        ++cycle_;
        uint32_t lo, up;
        std::memcpy(&lo, &micro_[pc * 8], 4);
        std::memcpy(&up, &micro_[pc * 8 + 4], 4);
        const bool ibit = (up >> 31) & 1;
        if ((up >> 30) & 1) ebit = 2;
        if ((up >> 27) & 3) ++stats_.debug_bits;

        // A espera: os VF lidos que ainda estão no pipeline do FMAC, Q/P ocupados.
        uint64_t need = cycle_;
        auto wait_reads = [&](const Reads& rd) {
            for (int k = 0; k < 3; ++k) {
                if (rd.reg[k] <= 0) continue;
                for (int l = 0; l < 4; ++l)
                    if (lane_on(rd.mask[k], l) && ready[rd.reg[k]][l] > need) need = ready[rd.reg[k]][l];
            }
        };
        wait_reads(upper_reads(up));
        if (!ibit) {
            wait_reads(lower_reads(lo));
            const uint32_t op = lo >> 25;
            if (op == 0x40 && (lo & 0x3c) == 0x3c) {
                const uint32_t sp = (((lo >> 6) & 0x1f) << 2) | (lo & 3);
                if (sp >= 0x38 && sp <= 0x3b && fdiv_.active) need = std::max(need, fdiv_.start + fdiv_.latency);
                if (((sp >= 0x70 && sp <= 0x7e) || sp == 0x64) && efu_.active &&
                    (sp == 0x7b || sp != 0x64))
                    need = std::max(need, efu_.start + efu_.latency);
            }
        }
        if (need > cycle_) {
            stats_.stall_cycles += need - cycle_;
            cycle_ = need;
        }
        flush_pipes();
        if (vi_backup_cycles_ > 0) --vi_backup_cycles_;

        // A de cima calcula com os valores de antes; a de baixo roda; a de cima escreve.
        const UpperResult u = upper(up);
        if (u.reg == -2) fail("instrução de cima desconhecida", pc, lo, up);
        int64_t target = -1;
        if (!ibit) {
            int lreg = -1;
            const bool lw = lower_writes_vf(lo, lreg);
            const bool discard = lw && lreg > 0 && !u.to_acc && u.reg == lreg && u.mask != 0;
            if (!discard) {
                try {
                    lower(lo, pc, target);
                } catch (const VuError&) {
                    fail("instrução de baixo desconhecida", pc, lo, up);
                }
                if (lw && lreg > 0)
                    for (int l = 0; l < 4; ++l)
                        if (lane_on(vu_dest(lo), l)) ready[lreg][l] = cycle_ + kFmacLatency;
            }
        }
        commit_upper(u);
        if (!u.to_acc && u.reg > 0)
            for (int l = 0; l < 4; ++l)
                if (lane_on(u.mask, l)) ready[u.reg][l] = cycle_ + kFmacLatency;
        if (ibit) r_.i = lo;
        // O instantâneo dos indicadores entra no pipeline do FMAC (toda instrução de cima menos o NOP).
        if (!((up & 0x3c) == 0x3c && ((((up >> 6) & 0x1f) << 2) | (up & 3)) == 0x2f)) {
            Pending p;
            p.start = cycle_;
            p.latency = kFmacLatency;
            p.mac = mac_now_;
            p.status = status_now_;
            p.clip = clip_now_;
            p.active = true;
            fmac_.push_back(p);
        }
        ++stats_.instructions;

        // Desvios e o fim.
        uint32_t next = (pc + 1) & micro_mask_;
        if (target >= 0) {
            if (branch > 0) {  // desvio no delay slot de outro: roda uma instrução no primeiro alvo
                ++stats_.branch_in_delay_slot;
                delay_branch = 1;
                delay_branch_pc = uint32_t(target);
            } else {
                branch = 2;
                branch_pc = uint32_t(target);
            }
        }
        if (branch > 0 && --branch == 0) next = branch_pc;
        else if (delay_branch > 0 && branch == 0 && --delay_branch == 0) next = delay_branch_pc;
        if (ebit > 0 && --ebit == 0) {
            flush_all();
            vi_backup_cycles_ = 0;
            stats_.cycles = cycle_;
            return;
        }
        pc = next;
    }
}

bool VuMachine::decodes(uint32_t lo, uint32_t up) {
    static VuMachine scratch(VuUnit::VU1);
    if (scratch.upper(up).reg == -2) return false;
    if ((up >> 31) & 1) return true;  // bit I: a de baixo é um float
    try {
        int64_t target;
        scratch.lower(lo, 0, target);
    } catch (const VuError&) {
        return false;
    }
    return true;
}

bool VuMachine::macro(uint32_t word) {
    const uint32_t w = word & 0x1ffffff;
    const uint32_t fn = w & 0x3f;
    ++cycle_;
    if (fn == 0x38 || fn == 0x39) return false;  // VCALLMS / VCALLMSR
    if (fn >= 0x30 && fn <= 0x35) {
        int64_t target;
        lower(0x80000000u | w, 0, target);  // VIADD, VISUB, VIADDI, VIAND, VIOR: o grupo 0x40 do modo micro
        vi_backup_cycles_ = 0;
        return true;
    }
    if ((fn & 0x3c) == 0x3c) {
        const uint32_t sp = (((w >> 6) & 0x1f) << 2) | (w & 3);
        if (sp >= 0x30) {
            lower_special(w);
            vi_backup_cycles_ = 0;
            flush_all();
            return true;
        }
    }
    const UpperResult u = upper(w);
    if (u.reg == -2) throw VuError("instrução COP2 desconhecida", 0, word, 0);
    commit_upper(u);
    r_.mac = mac_now_;
    r_.status = status_now_;
    r_.clip = clip_now_;
    return true;
}

}  // namespace ps2
