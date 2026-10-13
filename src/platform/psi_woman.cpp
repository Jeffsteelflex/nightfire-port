// O fundo animado da pausa: PS2SetUpWoman, psiDecompressWoman e o trecho do CreateSpaceAndPackTextures que prepara a
// textura 0x030000db. Ver psi_woman.h (o que é, a forma no PC e as divergências).
//
// Crédito: o estudo do nightfire-cxbx (NightfireResearch), docs/cxbx.md 3.1, apontou a função e o que ela faz; o porte
// é do MIPS do PS2 (a desmontagem de 0x001202a8 e do CreateSpaceAndPackTextures, 0x0011dcf8).

#include "platform/psi_woman.h"

#include <cstdio>
#include <cstring>

#include "engine/hashtable.h"
#include "engine/map_types.h"
#include "platform/psi.h"
#include "platform/psi_gfx.h"

extern uint32_t PS2FramesToSkip;  // engine/menu.cpp

uint8_t* pWoman = nullptr;
uint8_t* PS2Woman = nullptr;
uint8_t* WomanBuf = nullptr;
uint32_t WomanFrame = 0;
PsiWomanStats g_psi_woman;

namespace {

constexpr int kWomanWidth = 128;
constexpr int kFireWidth = 128;
// O maior deslocamento da fonte no fogo: 63 x 0x100 + 0x80, mais o quadro inteiro (0x4000).
constexpr uint32_t kFireSpan = 63u * 0x100u + 0x80u + uint32_t(kWomanBytes);

uint8_t woman_scratchpad[kWomanBytes];  // o scratchpad do EE (0x70000000), onde o PS2 decodifica o quadro
const uint8_t* woman_end = nullptr;      // só do port: o fim do item
int32_t skipit = 2;                      // skipit.288 (0x0030c9d8), estático: 2 no .sdata, não volta na carga

// A textura registrada com o hash (o maptex_tag do hashtable e o registro dele), ou nulos.
const pstex_tag* texture_record(uint32_t hash) {
    const auto* item = static_cast<const maptex_tag*>(hashtable_getitem(hash));
    return item != nullptr ? nf::raw(item->rec) : nullptr;
}

// As texturas como o disco as tem (8 bits, 256 cores, um quadro, as cópias no heap): a 0x030000db de 128 x 128 e o fogo
// de 128 de largura com as linhas que o RLE lê. Fora disso, o PS2 leria fora (o port conta e não faz nada).
bool woman_texture_ok(const pstex_tag* r) {
    return r != nullptr && r->colors == 0x100 && r->width == kWomanWidth && r->height >= kWomanBytes / kWomanWidth &&
           r->frames >= 1 && r->pixels != nullptr && r->palette != nullptr;
}
bool fire_texture_ok(const pstex_tag* r) {
    return r != nullptr && r->colors == 0x100 && r->width == kFireWidth &&
           uint32_t(r->width) * uint32_t(r->height) * r->frames >= kFireSpan && r->pixels != nullptr &&
           r->palette != nullptr;
}

// Só do port: o backend gráfico troca a textura (o que no PS2 é o espaço dela subindo para a VRAM no próximo quadro que
// a usar: CacheTexture o sobe uma vez por CacheFrame).
void send_to_backend(const pstex_tag* r) {
    g_psi_woman.texture = r->tex_index;
    if (g_psi_gfx_backend.update_texture == nullptr) return;
    bmpheader_tag header{};
    header.flags = 1;  // 8 bits por pixel
    header.width1 = uint16_t(kWomanWidth - 1);
    header.height1 = uint16_t(kWomanBytes / kWomanWidth - 1);
    header.frames = 1;
    g_psi_gfx_backend.update_texture(g_psi_gfx_backend.user, r->tex_index, header, r->pixels, uint32_t(kWomanBytes),
                                     r->palette, 0x400);
    ++g_psi_woman.updates;
}

}  // namespace

void psi_woman_debug_power_on() {
    pWoman = PS2Woman = WomanBuf = nullptr;
    WomanFrame = 0;
    woman_end = nullptr;
    skipit = 2;
    g_psi_woman = {};
}

// [decomp] 0x00120c20 PS2SetUpWoman
// Arquivo do tipo 16 do pacote ("woman"). 0x70000000 é o scratchpad do EE: no PC, um buffer do port.
void PS2SetUpWoman(uint8_t* data) {
    pWoman = data;
    WomanBuf = woman_scratchpad;
    PS2Woman = data;
    WomanFrame = 0;
    woman_end = nullptr;
    g_psi_woman.item_bytes = 0;
    g_psi_woman.last_frame = -1;
}

void psi_woman_set_end(const uint8_t* end) {
    woman_end = end;
    g_psi_woman.item_bytes = end != nullptr && PS2Woman != nullptr && end >= PS2Woman ? uint32_t(end - PS2Woman) : 0;
}

// O trecho do CreateSpaceAndPackTextures (0x0011dcf8, no laço dos 1024 espaços) que trata as duas texturas: o espaço do
// fogo fica como está (sem o Conv8to32: o psiDecompressWoman o lê linear); o da 0x030000db recebe a paleta do fogo
// (amemcpy de 0x400 bytes do registro do fogo para o dela) e é zerado (memset de 0x4000): o desenho guardado no pacote
// nunca sobe para a VRAM, e até o primeiro quadro a textura é a cor 0 do fogo (preto: o painel aditivo não mostra nada).
// No PC os "espaços" são as cópias no heap do pstex_tag; o backend troca a textura.
void psi_woman_pack_textures() {
    const pstex_tag* fire = texture_record(kWomanFireTexture);
    const pstex_tag* woman = texture_record(kWomanTexture);
    g_psi_woman.texture = -1;
    g_psi_woman.last_frame = -1;
    if (woman == nullptr) return;  // sem o espaço da 0x030000db o PS2 não faz nada
    if (!woman_texture_ok(woman) || !fire_texture_ok(fire)) {
        // Divergência: sem o fogo o PS2 copiaria a paleta do endereço 0.
        ++g_psi_woman.guards;
        return;
    }
    amemcpy(nf::raw(woman->palette), nf::raw(fire->palette), 0x400);
    std::memset(nf::raw(woman->pixels), 0, size_t(kWomanBytes));
    ++g_psi_woman.packs;
    send_to_backend(woman);
}

// [decomp] 0x001202a8 psiDecompressWoman
// Um tick sim, um não (skipit.288 desce PS2FramesToSkip por tick e volta a 2: a 60 quadros, um quadro do RLE a cada dois
// ticks; a 30, um por tick), decodifica um quadro do RLE no scratchpad: os pares alternam, começando por um trecho
// zerado (u16 big-endian: quantos bytes) e depois um bloco (u16 big-endian: quantos trechos, cada um com um u8 de
// tamanho, alternando cópia do fogo e zeros), até os 0x4000 bytes do quadro. A fonte anda junto com o destino a partir
// de (WomanFrame & 0x3f) x 0x100 + 0x80 - (WomanFrame & 0x7f) no espaço do fogo (128 de largura: duas linhas e uma
// coluna por quadro, o fogo rola para cima e para o lado). Depois o Conv8to32 leva o quadro para o espaço da 0x030000db
// (no PC: os pixels da textura e o backend; ver psi_woman.h), e no quadro 348 o RLE volta ao começo.
void psiDecompressWoman() {
    const int32_t frames = PS2FramesToSkip != 0 ? int32_t(PS2FramesToSkip) : 1;  // o port deixa 0: vale 1
    skipit -= frames;
    if (skipit > 0) return;
    skipit = 2;
    const pstex_tag* fire = texture_record(kWomanFireTexture);
    if (fire == nullptr) return;
    const pstex_tag* woman = texture_record(kWomanTexture);
    if (pWoman == nullptr || WomanBuf == nullptr || !fire_texture_ok(fire) || !woman_texture_ok(woman)) {
        // Divergência: o PS2 leria o RLE de um item que não há, ou a 0x030000db sem conferir o nulo.
        ++g_psi_woman.guards;
        return;
    }
    const uint8_t* const fire_pixels = nf::raw(fire->pixels);
    uint32_t src = (WomanFrame & 0x3f) * 0x100 + (0x80 - (WomanFrame & 0x7f));
    uint8_t* dst = WomanBuf;
    int32_t left = kWomanBytes;
    int8_t zeros = 1;
    // Só do port: o RLE não passa do quadro (take) nem do item, quando o fim é conhecido (next_byte).
    const auto take = [&](uint32_t n) {
        if (int32_t(n) > left) {
            ++g_psi_woman.guards;
            left = 0;
            return false;
        }
        return true;
    };
    const auto next_byte = [&]() -> uint8_t {
        if (woman_end != nullptr && pWoman >= woman_end) {
            ++g_psi_woman.guards;
            left = 0;
            return 0;
        }
        return *pWoman++;
    };
    do {
        if (zeros == 1) {
            const uint8_t hi = next_byte();
            const uint32_t n = uint32_t(hi) << 8 | next_byte();
            if (left == 0 || !take(n)) break;
            left -= int32_t(n);
            std::memset(dst, 0, n);
            dst += n;
            src += n;
        } else {
            const uint8_t hi = next_byte();
            const uint32_t runs = uint32_t(hi) << 8 | next_byte();
            int8_t copy = 1;
            for (uint32_t i = 0; i < runs && left != 0; ++i) {
                const uint8_t n = next_byte();
                if (left == 0 || !take(n)) break;
                if (copy == 1) {
                    std::memcpy(dst, fire_pixels + src, n);
                } else {
                    std::memset(dst, 0, n);
                }
                left -= n;
                dst += n;
                src += n;
                copy = int8_t(1 - copy);
            }
        }
        zeros = int8_t(1 - zeros);
    } while (left != 0);
    // Conv8to32(largura, altura do espaço, WomanBuf, Space[...].ptr) para o espaço da 0x030000db; no PC, a imagem linear.
    std::memcpy(nf::raw(woman->pixels), WomanBuf, size_t(kWomanBytes));
    g_psi_woman.last_frame = int32_t(WomanFrame);
    ++g_psi_woman.decoded;
    send_to_backend(woman);
    ++WomanFrame;
    if (WomanFrame == kWomanFrames) {
        g_psi_woman.cycle_bytes = uint32_t(pWoman - PS2Woman);
        WomanFrame = 0;
        pWoman = PS2Woman;
    }
}

void psi_woman_describe(char* out, size_t size) {
    std::snprintf(out, size, "fundo_pausa quadro %d proximo %u decodificados %u trocas %u preparos %u textura %d guardas %u",
                  g_psi_woman.last_frame, WomanFrame, g_psi_woman.decoded, g_psi_woman.updates, g_psi_woman.packs,
                  g_psi_woman.texture, g_psi_woman.guards);
}
