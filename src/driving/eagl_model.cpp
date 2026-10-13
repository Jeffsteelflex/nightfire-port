// O EAGL::Model e o EAGL::GeoPrim (eagl_model.h), portados do DRIVING.ELF.

#include "driving/eagl_model.h"

#include <cstring>

#include "driving/eagl_state.h"

namespace drv::eagl {

namespace {

// Os globais do construtor das primitivas (0x002a5740).
uint32_t g_prev_call = 0;      // DAT_00348c08: a lista de etiquetas CALL da primitiva anterior (só DMA)
uint32_t g_prev_end_slot = 0;  // DAT_00348c04: a palavra 0 do fim da lista de comandos anterior
uint32_t g_dma = 0;            // DAT_00348c0c: os qwords de DMA (op 4) da primitiva corrente
int g_merges = 0;              // DAT_00348c10
uint32_t g_cmd_base = 0;       // só do port: a base dos saltos 0x10/0x11 (ver eagl_model.h)

// O microprograma corrente do desenho (DAT_00348c00) e o "alocador" da memória de código da VU1.
uint32_t g_cur_microcode = 0;
uint32_t g_vu_code_next = 0;
std::vector<uint32_t>& resident() {
    static std::vector<uint32_t> v;
    return v;
}

uint32_t* W(uint32_t a) { return ptr_at<uint32_t>(a); }

// O tamanho (em palavras, com a operação) das operações, como o construtor (0x002a5740) as pula.
int op_words(uint32_t w) {
    const uint32_t n = w & 0xFFFF;
    switch (w >> 24) {
    case 1: return 2;
    case 3: case 0x14: return 1 + int(n);
    case 4: return 2;
    case 5: case 6: case 7: case 0xc: case 0xd: return 1 + int(n) + 2;
    case 8: case 9: return 1 + int(n) + 4;
    case 10: case 0xb: return 1 + int(n) + 3;
    case 0x15: case 0x17: case 0x19: return 1 + int(n) + 1;
    case 0x1b: return 1 + int((w >> 16) & 0xFF) + int(n) + 1;
    default: return 1;
    }
}

}  // namespace

// [driving] 0x002a5740 EAGL::GeoPrim::Construct
// As primitivas de um grupo viram uma só: a lista de comandos de cada uma (menos a primeira) é ligada ao fim da anterior
// por um salto (op 0x10), e uma primitiva que só manda vértices (só o op 4, uma etiqueta CALL de 0 qwords) depois de
// outra igual tem o pacote dela emendado no da anterior (o RET do pacote vira NEXT). Devolve false para quem saiu da
// lista. Divergência: o salto relativo à base (eagl_model.h).
bool GeoPrim_Construct(uint32_t geoprim, uint32_t /*handle*/, int index_in_group) {
    bool keep = true;
    bool other_ops = index_in_group == 0;
    if (index_in_group == 0) {
        g_prev_call = 0;
        g_prev_end_slot = 0;
    }
    uint32_t p = *W(geoprim);
    for (int guard = 0; guard < 100000; ++guard) {
        const uint32_t w = *W(p);
        if (w == 0) {
            const uint32_t dma = g_dma;
            auto is_call0 = [](uint32_t a) {
                const uint32_t t = *W(a);
                return ((t >> 28) & 0xF) == 5 && (t & 0xFFFFFFF) == 0;
            };
            if (!other_ops && dma && is_call0(dma) && g_prev_call) {
                ++g_merges;
                int n = 0;
                for (uint32_t t = dma; ((*W(t) >> 28) & 0xF) == 5; t += 16) ++n;
                for (int i = 0; i < n; ++i) {
                    uint32_t q = *W(g_prev_call + uint32_t(i) * 16 + 4);  // o pacote chamado pela anterior
                    uint32_t t = *W(q);
                    while (((t >> 28) & 0xF) == 5) {
                        q += ((t & 0xFFFFFFF) + 1) * 16;
                        t = *W(q);
                    }
                    // A etiqueta do fim (o RET) vira NEXT para o pacote desta primitiva.
                    W(q)[0] = (t & 0xFFFFFFF) | 0x20000000;
                    W(q)[1] = *W(dma + uint32_t(i) * 16 + 4);
                }
                g_prev_call = dma;
                return false;
            }
            if (g_prev_end_slot) {
                const uint32_t target = *W(geoprim) - g_cmd_base;
                *W(g_prev_end_slot) = ((target >> 2) & 0xFFFFFF) | (target < 0x4000000 ? 0x10000000u : 0x11000000u);
                keep = false;
            }
            g_prev_end_slot = p;
            g_prev_call = 0;
            if (dma && is_call0(dma)) g_prev_call = dma;
            return keep;
        }
        const uint32_t op = w >> 24;
        if (op == 4) {
            g_dma = W(p)[1];
            p += 8;
            continue;
        }
        if (op != 2 && op != 0x10 && op != 0x11 && op != 0x16 && op != 0x18 && op != 0x1a) other_ops = true;
        p += uint32_t(op_words(w)) * 4;
    }
    return keep;
}

// [driving] 0x00284c40 EAGL::Model::Model
void Model_Construct(uint32_t model) {
    uint32_t write = *W(model + 0xcc);
    uint32_t read = write;
    const uint32_t first_level = *W(read) & 0xFFFF;
    uint32_t count_slot = 0;
    uint32_t kept = 0;
    int index = 0;
    for (int guard = 0; guard < 1000000; ++guard) {
        uint32_t w = *W(read);
        if ((w & 0xFFFF) == 0xFFFF) {
            if (w & 0x10000000) return;  // já construído
            *W(read) = w | 0x10000000;
            const uint32_t handle = W(read)[1];
            read += 8;
            const bool keep = GeoPrim_Construct(*W(handle), handle, index);
            ++index;
            if (keep) {
                kept += 2;
                W(write)[0] = *W(read - 8);
                W(write)[1] = *W(read - 4);
                write += 8;
            }
        } else {
            if (count_slot) *W(count_slot) = kept;
            index = 0;
            kept = 0;
            W(write)[0] = W(read)[0];
            W(write)[1] = W(read)[1];
            count_slot = write + 8;
            W(write)[2] = W(read)[2];
            write += 12;
            read += 12;
        }
        w = *W(read);
        if ((w & 0xFFFF) <= first_level) {
            *W(write) = *W(read);
            if (count_slot) *W(count_slot) = kept;
            return;
        }
    }
}

DmaBuffer::DmaBuffer(size_t capacity_qw) : store_(capacity_qw * 16 + 16) {
    base_ = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(store_.data()) + 15) & ~uintptr_t(15));
    cur_ = base_;
    cap_ = capacity_qw * 16;
}
bool DmaBuffer::room(size_t bytes) {
    if (size_t(cur_ - base_) + bytes <= cap_) return true;
    overflowed_ = true;
    return false;
}
void DmaBuffer::put32(uint32_t v) {
    if (!room(4)) return;
    std::memcpy(cur_, &v, 4);
    cur_ += 4;
}
void DmaBuffer::put64(uint64_t v) {
    if (!room(8)) return;
    std::memcpy(cur_, &v, 8);
    cur_ += 8;
}
void DmaBuffer::copy(const void* src, size_t bytes) {
    if (!room(bytes)) return;
    if (src) {
        std::memcpy(cur_, src, bytes);
    } else {
        std::memset(cur_, 0, bytes);
        ++null_sources_;
    }
    cur_ += bytes;
}
void DmaBuffer::align16() {
    while (reinterpret_cast<uintptr_t>(cur_) & 15) put32(0);
}

void Model_SetCommandBase(uint32_t base) { g_cmd_base = base; }

void GeoPrim_ResetMicrocode() {
    g_cur_microcode = 0;
    g_vu_code_next = 0;
    for (uint32_t mc : resident()) W(mc)[2] = 0xFFFFFFFFu;
    resident().clear();
}

// [driving] 0x002a3290 EAGL::GeoPrim::Draw
// Portados: as operações 1, 3, 4, 5, 6, 7, 0xe, 0xf, 0x10-0x13 e o fim. As outras (2, 8-0xd, 0x14-0x1b: matrizes de
// paleta para pele, parâmetros por instância) não aparecem nos carros; são puladas e contadas (DrawStats).
// Divergências: o op 1 não manda o microcódigo (MPG), só marca o microprograma como residente num endereço da memória
// de código (ele não roda no PC); o op 3 (subir as texturas para a VRAM) não faz nada; as contagens de desempenho
// (Perf, Count, PCR0/1) não existem.
uint32_t GeoPrim_Draw(uint32_t geoprim, DmaBuffer& out, const DrawContext& ctx, DrawStats& stats) {
    ++stats.geoprims;
    uint32_t p = *W(geoprim);
    uint8_t* open_cnt = nullptr;
    uint8_t* rep_start = nullptr;
    uint32_t rep_size = 0;
    auto close_cnt = [&]() {
        out.align16();
        const uint32_t qwc = uint32_t((out.cur() - open_cnt) >> 4) - 1;
        *reinterpret_cast<uint64_t*>(open_cnt) = uint64_t(qwc) | 0x10000000ull;
        *reinterpret_cast<uint64_t*>(open_cnt + 8) = 0;
        open_cnt = nullptr;
    };
    for (int guard = 0; guard < 1000000; ++guard) {
        const uint32_t w = *W(p);
        if (w == 0) {
            if (open_cnt) close_cnt();
            break;
        }
        const uint32_t op = w >> 24;
        const uint32_t n = w & 0xFFFF;
        switch (op) {
        case 1: {  // o microprograma
            const uint32_t mc = W(p)[1];
            p += 8;
            if (W(mc)[2] == 0xFFFFFFFFu) {
                const uint32_t instr = (W(mc)[0] + 1) & ~1u;
                if (g_vu_code_next + instr > 0x800) GeoPrim_ResetMicrocode();
                W(mc)[2] = g_vu_code_next;
                g_vu_code_next += instr;
                resident().push_back(mc);
                ++stats.microcode_uploads;
            }
            if (g_cur_microcode != mc) {
                const uint32_t setup = W(mc)[3];
                *W(setup + 0x2c) = W(mc)[2] | 0x14000000u;  // MSCAL no endereço do microprograma
                out.copy(ptr_at(setup), 48);
                g_cur_microcode = mc;
                ++stats.microcode_switches;
                stats.microcode_sequence.push_back(mc);
            }
            break;
        }
        case 3:  // as texturas: no PS2 garante que estão na VRAM
            p += 4 + n * 4;
            break;
        case 4: {  // qwords de DMA prontos
            const uint32_t src = W(p)[1] + uint32_t(ctx.frame) * 16;
            out.copy(src < 0x10000 ? nullptr : ptr_at(src), size_t(n) * 8);  // só do port: endereço inválido = zeros
            p += 8;
            break;
        }
        case 5:
        case 7: {  // palavras do VIF e uma cópia dos dados
            const uint32_t n64 = W(p)[1];
            const uint32_t src = W(p)[2];
            const uint32_t pad = (w >> 16) & 0xFF;
            out.copy(W(p) + 3, size_t(n) * 4);
            p += (3 + n) * 4;
            uint8_t* data = out.cur();
            out.copy(src < 0x10000 ? nullptr : ptr_at(src), size_t(n64) * 8);
            for (uint32_t k = 0; k < pad; ++k) reinterpret_cast<uint32_t*>(out.cur())[-int(pad) + int(k)] = 0;
            if (op == 7) {
                uint64_t& regs = *reinterpret_cast<uint64_t*>(data + 0x28);
                uint64_t& frame = *reinterpret_cast<uint64_t*>(data + 0x50);
                uint64_t& zbuf = *reinterpret_cast<uint64_t*>(data + 0x60);
                uint64_t& fba = *reinterpret_cast<uint64_t*>(data + 0x70);
                frame = (regs & 0xF00) == 0xE00 ? (frame & 0xFFFFFFFF00000000ull) | uint32_t(ctx.gs_frame) : ctx.gs_frame;
                zbuf = (regs & 0xF000) == 0xE000 ? (zbuf & 0x100000000ull) | (ctx.gs_zbuf & ~0x100000000ull)
                                                 : ctx.gs_zbuf;
                fba = (regs & 0xF0000) == 0xE0000 ? (fba & 1) | (ctx.gs_fba & ~1ull) : ctx.gs_fba;
                regs &= 0xFFFFFFFFFFFEEEFFull;
            }
            break;
        }
        case 6: {  // o pacote GS de um TAR
            const uint32_t anim = (w >> 16) & 0xFF;
            uint32_t tar = W(p)[2];
            if (anim) tar += anim * uint32_t(ctx.frame) * 0x60;
            out.copy(W(p) + 3, size_t(n) * 4);
            p += (3 + n) * 4;
            if (tar < 0x10000) {
                // Só do port: um TAR que o carregador não resolveu (um símbolo GAME:: que o port ainda não
                // fornece). O PS2 leria a imagem do endereço baixo; aqui vai o TAR padrão (sem textura), contado
                // como fonte nula.
                tar = tar_create("\0\0\0\0", true, true);  // liberado com os outros em tar_images_reset
                out.note_null_source();
            }
            uint64_t* end = TAR_WritePacket(tar + 4, reinterpret_cast<uint64_t*>(out.cur()));
            out.advance(size_t(reinterpret_cast<uint8_t*>(end) - out.cur()));
            break;
        }
        case 0xe:  // abre uma etiqueta CNT
            open_cnt = out.cur();
            out.advance(16);
            p += 4;
            break;
        case 0xf:  // fecha
            if (!rep_size && rep_start) {
                out.align16();
                rep_size = uint32_t((out.cur() - rep_start) >> 4);
            }
            if (open_cnt) close_cnt();
            p += 4;
            break;
        case 0x10:
            p = ctx.cmd_base + (w & 0xFFFFFF) * 4;
            break;
        case 0x11:
            p = ctx.cmd_base + (w & 0xFFFFFF) * 4 + 0x4000000;
            break;
        case 0x12:  // reenvia por REF o bloco marcado
            out.put64(uint64_t(rep_size) | 0x30000000ull | uint64_t(addr_of(rep_start)) << 32);
            out.put64(0);
            p += 4;
            break;
        case 0x13:
            rep_start = out.cur();
            rep_size = 0;
            p += 4;
            break;
        default:
            ++stats.unported_ops;
            stats.last_unported_op = int(op);
            p += uint32_t(op_words(w)) * 4;
            break;
        }
    }
    return W(geoprim)[2];
}

std::vector<ModelGroup> Model_Groups(uint32_t model) {
    std::vector<ModelGroup> out;
    uint32_t p = *W(model + 0xcc);
    const uint32_t vis = *W(model + 0xc4);
    const uint32_t first_level = *W(p) & 0xFFFF;
    int gi = 0;
    ModelGroup* cur = nullptr;
    for (int guard = 0; guard < 1000000; ++guard) {
        const uint32_t w = *W(p);
        if ((w & 0xFFFF) == 0xFFFF) {
            const uint32_t handle = W(p)[1];
            p += 8;
            if (handle && cur) cur->geoprims.push_back(*W(handle));
        } else {
            ModelGroup g;
            g.index = gi;
            g.level = int(w & 0xFFFF);
            g.visible = vis == 0 || *ptr_at<uint16_t>(vis + uint32_t(gi) * 4 + 2) != 0;
            out.push_back(g);
            cur = &out.back();
            p += g.visible ? 12 : (W(p)[2] + 3) * 4;
            ++gi;
        }
        if ((*W(p) & 0xFFFF) <= first_level) break;
    }
    return out;
}

const char* GeoPrim_Name(uint32_t geoprim) {
    const uint32_t n = W(geoprim)[5];
    return n ? ptr_at<const char>(n) : "";
}

const char* RenderMethod_Name(uint32_t method) {
    if (!method) return "";
    const uint32_t n = W(method)[5];
    return n ? ptr_at<const char>(n) : "";
}

void register_eagl_types() {
    register_type(
        "Model",
        [](uint32_t addr, DynamicLoader& loader) {
            Model_SetCommandBase(addr_of(loader.data()));
            Model_Construct(addr);
        },
        nullptr);
    register_type("EAGL::TAR", [](uint32_t addr, DynamicLoader&) { TAR_Construct(addr); }, nullptr);
    register_runtime_alloc(
        "EAGL::GeoPrimState",
        [](const char*, DynamicLoader&, uint32_t&, bool&) -> uint32_t {
            ++runtime_alloc_unported;  // o analisador de 14 KB (0x0027fe80) não foi portado
            return addr_of(RStateManager_GetGeoPrimState(""));
        },
        nullptr);
    register_runtime_alloc(
        "EAGL::TAR",
        [](const char*, DynamicLoader&, uint32_t&, bool&) -> uint32_t {
            ++runtime_alloc_unported;  // 0x0027af10, não portado
            return tar_create("????", true, true);
        },
        nullptr);
}

}  // namespace drv::eagl
