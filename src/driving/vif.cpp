// O DMA do VIF1 e as UNPACK na memória da VU1 (vif.h; só do port, o hardware do PS2).

#include "driving/vif.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "platform/ptr32.h"

namespace drv {

float VuMemory::f(int qw, int c) const {
    float v;
    std::memcpy(&v, &w[((qw & 1023) << 2) | c], 4);
    return v;
}

Vif1::Vif1() { reset(); }

void Vif1::reset() {
    std::memset(mem.w, 0, sizeof mem.w);
    st = VifState();
    errors.clear();
    unpack_log.clear();
    unpacks = kicks = 0;
}

namespace {

// Bytes por elemento (vn = componentes - 1, vl: 0 32 bits, 1 16, 2 8, 3 5551).
int element_bits(int vl) { return vl == 0 ? 32 : vl == 1 ? 16 : vl == 2 ? 8 : 16; }

}  // namespace

void Vif1::unpack(uint32_t code, const uint32_t* data, size_t avail, size_t& used) {
    const int cmd = int(code >> 24) & 0x7F;
    const int vn = (cmd >> 2) & 3;  // 0 S, 1 V2, 2 V3, 3 V4
    const int vl = cmd & 3;
    const bool masked = (cmd & 0x10) != 0;
    int num = int(code >> 16) & 0xFF;
    if (num == 0) num = 256;
    const uint32_t imm = code & 0xFFFF;
    const bool usn = (imm >> 14) & 1;
    const bool flg = (imm >> 15) & 1;
    uint32_t addr = (imm & 0x3FF) + (flg ? st.tops : 0);
    unpack_log.push_back({cmd, addr & 1023, num, source_of(data)});
    const int comps = vn + 1;
    const int bits = (vn == 3 && vl == 3) ? 16 : element_bits(vl) * comps;  // V4-5 ocupa 16 bits
    // Quantos vetores vêm de dados: com escrita preenchida (CL < WL), só CL de cada WL.
    int from_data = num;
    if (st.cl < st.wl) from_data = st.cl * (num / st.wl) + std::min(num % st.wl, st.cl);
    const size_t total_bits = size_t(from_data) * bits;
    const size_t words = (total_bits + 31) / 32;
    used = words;
    if (words > avail) {
        errors.push_back("UNPACK além do fim dos dados");
        used = avail;
        return;
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    size_t bitpos = 0;
    auto read_elem = [&](int nbits) -> uint32_t {
        uint32_t v = 0;
        const size_t byte = bitpos / 8;
        if (nbits == 32) std::memcpy(&v, bytes + byte, 4);
        else if (nbits == 16) {
            uint16_t s;
            std::memcpy(&s, bytes + byte, 2);
            v = usn ? s : uint32_t(int32_t(int16_t(s)));
        } else {
            const uint8_t b = bytes[byte];
            v = usn ? b : uint32_t(int32_t(int8_t(b)));
        }
        bitpos += size_t(nbits);
        return v;
    };
    int cycle = 0;  // posição no ciclo CL/WL
    int written = 0;
    while (written < num) {
        const bool data_slot = (st.cl >= st.wl) ? true : (cycle % st.wl) < st.cl;
        uint32_t v[4] = {0, 0, 0, 0};
        if (data_slot) {
            if (vn == 3 && vl == 3) {  // V4-5: RGBA 5551
                uint16_t s;
                std::memcpy(&s, bytes + bitpos / 8, 2);
                bitpos += 16;
                v[0] = (s & 31) << 3;
                v[1] = ((s >> 5) & 31) << 3;
                v[2] = ((s >> 10) & 31) << 3;
                v[3] = (s >> 15) << 7;
            } else {
                const int eb = element_bits(vl);
                for (int c = 0; c < comps; ++c) v[c] = read_elem(eb);
                if (vn == 0) v[1] = v[2] = v[3] = v[0];
                else if (vn == 1) { v[2] = v[0]; v[3] = v[1]; }
                else if (vn == 2) v[3] = 0;
            }
        }
        const int mrow = std::min(cycle % std::max(st.wl, 1), 3);
        for (int c = 0; c < 4; ++c) {
            int m = masked ? int(st.mask >> ((mrow * 4 + c) * 2)) & 3 : 0;
            if (!data_slot && m == 0) m = 1;  // escrita preenchida: sem dado, o campo vem da linha
            uint32_t out;
            if (m == 0) {
                if (st.mode == 1) out = v[c] + st.row[c];
                else if (st.mode == 2) out = st.row[c] = v[c] + st.row[c];
                else out = v[c];
            } else if (m == 1) out = st.row[c];
            else if (m == 2) out = st.col[std::min(mrow, 3)];
            else continue;  // 3: protegido
            mem.w[((addr & 1023) << 2) | c] = out;
        }
        ++addr;
        ++written;
        ++cycle;
        // Escrita pulada (CL > WL): depois de WL vetores, pula CL - WL endereços.
        if (st.cl > st.wl && (cycle % st.wl) == 0) addr += uint32_t(st.cl - st.wl);
    }
    ++unpacks;
}

void Vif1::process(const uint32_t* w, size_t n) {
    size_t i = 0;
    while (i < n) {
        const uint32_t code = w[i];
        const int cmd = int(code >> 24) & 0x7F;
        const uint32_t imm = code & 0xFFFF;
        const int num = int(code >> 16) & 0xFF;
        ++i;
        if (cmd >= 0x60) {
            size_t used = 0;
            unpack(code, w + i, n - i, used);
            i += used;
            continue;
        }
        switch (cmd) {
        case 0x00: break;  // NOP
        case 0x01: st.cl = int(imm & 0xFF); st.wl = int((imm >> 8) & 0xFF); if (st.wl == 0) st.wl = 256; break;
        case 0x02: st.offset = imm & 0x3FF; st.dbf = false; st.tops = st.base; break;  // OFFSET
        case 0x03: st.base = imm & 0x3FF; break;                                       // BASE
        case 0x04: st.itops = imm & 0x3FF; break;                                      // ITOP
        case 0x05: st.mode = int(imm & 3); break;                                      // STMOD
        case 0x06: case 0x07: case 0x10: case 0x11: case 0x13: break;  // MSKPATH3, MARK, FLUSHE, FLUSH, FLUSHA
        case 0x14: case 0x15: case 0x17: {  // MSCAL, MSCALF, MSCNT
            st.top = st.tops;
            st.itop = st.itops;
            ++kicks;
            if (on_kick) on_kick(cmd == 0x14 ? kMscal : cmd == 0x15 ? kMscalf : kMscnt, imm, st.top, st.itop);
            unpack_log.clear();
            st.tops = st.dbf ? st.base : st.base + st.offset;
            st.dbf = !st.dbf;
            break;
        }
        case 0x20:  // STMASK
            if (i < n) st.mask = w[i++];
            break;
        case 0x30:
        case 0x31:  // STROW, STCOL
            for (int c = 0; c < 4 && i < n; ++c) (cmd == 0x30 ? st.row : st.col)[c] = w[i++];
            break;
        case 0x4A:  // MPG: o microcódigo não roda no PC
            i += size_t(num ? num : 256) * 2;
            break;
        case 0x50:
        case 0x51: {  // DIRECT, DIRECTHL: os dados começam no qword seguinte
            const size_t qwc = imm ? imm : 65536;
            i = (i + 3) & ~size_t(3);
            if (i + qwc * 4 > n) {
                errors.push_back("DIRECT além do fim");
                return;
            }
            if (on_direct) on_direct(w + i, int(qwc));
            i += qwc * 4;
            break;
        }
        default: {
            char b[64];
            std::snprintf(b, sizeof b, "comando do VIF desconhecido 0x%02x", cmd);
            errors.push_back(b);
            break;
        }
        }
    }
}

const uint32_t* Vif1::source_of(const uint32_t* p) const {
    if (!stream_ || p < stream_ || p >= stream_ + stream_words_) return nullptr;
    const size_t off = size_t(p - stream_);
    for (const Segment& g : segments_)
        if (off >= g.start && off < g.start + g.words) return nf::p32_ptr<const uint32_t>(g.addr) + (off - g.start);
    return nullptr;
}

void Vif1::run_dma_chain(uint32_t tag_addr, int max_tags) {
    std::vector<uint32_t> stream;
    uint32_t stack[2];
    int sp = 0;
    uint32_t t = tag_addr;
    segments_.clear();
    auto push_qw = [&](uint32_t a, uint32_t qwc) {
        const uint32_t* p = nf::p32_ptr<const uint32_t>(a);
        if (qwc) segments_.push_back({stream.size(), size_t(qwc) * 4, a});
        stream.insert(stream.end(), p, p + size_t(qwc) * 4);
    };
    for (int guard = 0; guard < max_tags && t; ++guard) {
        const uint32_t* tag = nf::p32_ptr<const uint32_t>(t);
        const uint32_t qwc = tag[0] & 0xFFFF;
        const int id = int(tag[0] >> 28) & 7;
        const uint32_t addr = tag[1];
        stream.push_back(tag[2]);  // TTE: as duas palavras de cima da etiqueta vão para o VIF
        stream.push_back(tag[3]);
        bool end = false;
        switch (id) {
        case 0:  // REFE
            push_qw(addr, qwc);
            end = true;
            break;
        case 1:  // CNT
            push_qw(t + 16, qwc);
            t = t + 16 + qwc * 16;
            break;
        case 2:  // NEXT
            push_qw(t + 16, qwc);
            t = addr;
            break;
        case 3:
        case 4:  // REF, REFS
            push_qw(addr, qwc);
            t += 16;
            break;
        case 5:  // CALL
            push_qw(t + 16, qwc);
            if (sp >= 2) {
                errors.push_back("DMA: CALL com a pilha cheia");
                end = true;
                break;
            }
            stack[sp++] = t + 16 + qwc * 16;
            t = addr;
            break;
        case 6:  // RET
            push_qw(t + 16, qwc);
            if (sp == 0) end = true;
            else t = stack[--sp];
            break;
        default:  // END
            push_qw(t + 16, qwc);
            end = true;
            break;
        }
        if (end) break;
    }
    stream_ = stream.data();
    stream_words_ = stream.size();
    process(stream.data(), stream.size());
    stream_ = nullptr;
    stream_words_ = 0;
}

}  // namespace drv
