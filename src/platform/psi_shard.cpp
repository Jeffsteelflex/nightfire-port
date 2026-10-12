// Camada de plataforma dos quebráveis e dos estilhaços. Ver psi_shard.h.

#include "platform/psi_shard.h"

#include <cstring>

#include "engine/break.h"
#include "engine/loader.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "platform/psi_draw.h"
#include "platform/psi_gfx.h"

uint32_t ShardTexture = 0;
ShardGuards g_shard_guards;
int32_t g_psi_font_texture = -1;

namespace {

// A cadeia de um estilhaço (psiCreateShard), em palavras: os três vértices (posição, cor inteira,
// quadword zerada) a partir de 0x12 e as UVs (u, v, 1, 0) a partir de 0x37.
constexpr int kShardVertWord = 0x12;
constexpr int kShardUvWord = 0x37;
constexpr uint32_t kShardChainBytes = 0x120;

uint32_t* ptr_word(uint32_t v) { return nf::p32_ptr<uint32_t>(v); }

// Cabeçalho do bloco 0x2d depois de psiCreateEntityGfx: {caixas, ponteiro das caixas, extra}.
uint32_t* gfx_header(const celglist_tag* glist) {
    if (glist == nullptr || glist->gfx == nullptr) return nullptr;
    return reinterpret_cast<uint32_t*>(nf::raw(glist->gfx->ps2_gfx));
}

uint32_t box0_word(const uint32_t* hdr, int offset) {
    const uint8_t* box = reinterpret_cast<const uint8_t*>(ptr_word(hdr[1]));
    uint32_t v;
    std::memcpy(&v, box + offset, 4);
    return v;
}

float word_float(uint32_t w) {
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}

uint32_t float_word(float f) {
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return w;
}

}  // namespace

void psi_shard_debug_power_on() {
    ShardTexture = 0;
    g_shard_guards = {};
    g_psi_font_texture = -1;
}

// [decomp] 0x00117418 psiCreateBreakableData
// Lê a cadeia de DMA da caixa 0 do modelo (uma vez por modelo: o registro de 0x18 bytes fica no
// terceiro campo do cabeçalho do bloco 0x2d) e guarda, na ordem dos lotes, a posição (UNPACK V3-32),
// a UV (V2-32) e a cor (V4-8 sem sinal, cada byte vira float) de cada vértice, e um triângulo para
// cada vértice de tira sem o bit ADC (V4-8 com sinal: o bit 7 do byte w das normais) a partir do
// terceiro do lote, com a textura da última etiqueta REF. Como no original, os índices do triângulo
// contam a partir do começo do lote (não do começo da lista de vértices): nos modelos com mais de um
// lote os triângulos dos lotes seguintes apontam para os vértices do primeiro. Os pacotes DIRECT são
// pulados por imm & 0xff quadwords a partir do próprio código (cai na última palavra dos dados, um
// endereço A+D que vale 0 = NOP).
// Divergências: modelo sem bloco 0x2d ou sem caixa (o original leria endereços baixos) sai antes;
// códigos de DMA ou VIF que o original não sabe pular (ficaria num laço infinito) param a leitura; e
// os vértices e triângulos além do que a caixa reserva (+0x2c, +0x30; o original escreveria além do
// bloco) não são gravados. Tudo contado em g_shard_guards. O registro novo de 0x18 bytes é zerado (o
// original deixa lixo nos ponteiros se a caixa não tiver cadeia, e psiGetTriList os leria).
void psiCreateBreakableData(celglist_tag* glist) {
    uint32_t texture = 0;
    uint32_t* hdr = gfx_header(glist);
    if (hdr == nullptr) return;
    auto* data = reinterpret_cast<BREAKGFX_tag*>(ptr_word(hdr[2]));
    if (data == nullptr) {
        data = static_cast<BREAKGFX_tag*>(Mem_Malloc(0x18, 0x904, 0));
        if (data != nullptr) std::memset(data, 0, sizeof(*data));  // port: o original não zera
        hdr[2] = nf::p32_u(data);
    } else if (data->tris != nullptr) {
        return;
    }
    if (data == nullptr || hdr[1] == 0) return;
    uint32_t* p = ptr_word(box0_word(hdr, 0x20));
    if (p == nullptr) return;
    const uint32_t tri_count = box0_word(hdr, 0x30);
    const uint32_t vert_count = box0_word(hdr, 0x2c);
    auto* tri = static_cast<BREAKTRI_tag*>(Mem_Malloc(tri_count * 0xc, 0x904, 0));
    data->tris = tri;
    auto* verts = static_cast<SHATTER_VERT*>(Mem_Malloc(vert_count << 6, 0x904, 0));
    data->verts = verts;
    uint32_t xyz = 0, uv = 0, colour = 0, tris = 0;
    uint32_t tag = *p;
    while (tag != 0x60000000) {
        if ((tag & 0xf0000000) == 0x10000000) {  // CNT: os códigos VIF vêm depois das duas palavras da etiqueta
            uint32_t* end = p + ((tag & 0xffff) + 1) * 4;
            p += 2;
            while (p != end) {
                const uint32_t code = *p;
                const uint32_t cmd = code & 0xff000000;
                if (cmd == 0x50000000 || cmd == 0x51000000) {  // DIRECT, DIRECTHL
                    p += (code & 0xff) * 4;
                } else if (cmd == 0 || cmd == 0x01000000 || cmd == 0x10000000 || cmd == 0x17000000) {
                    ++p;  // NOP, STCYCL, FLUSHE, MSCNT
                } else if (cmd == 0x64000000 || cmd == 0x68000000 || cmd == 0x6c000000 || cmd == 0x6e000000) {
                    const uint32_t num = (code >> 16) & 0x7f;
                    const int32_t bits = (0x20 >> ((code >> 24) & 3)) * int32_t(((code >> 26) & 3) + 1);
                    const int32_t words = (bits * int32_t(num) + 0x20) >> 5;
                    const uint32_t* d = p + 1;
                    if (cmd == 0x68000000) {  // V3-32: posição
                        for (uint32_t k = 0; k < num; ++k, d += 3, ++xyz) {
                            if (xyz >= vert_count) {
                                ++g_shard_guards.vert_overflow;
                                continue;
                            }
                            verts[xyz].pos.x = word_float(d[0]);
                            verts[xyz].pos.y = word_float(d[1]);
                            verts[xyz].pos.z = word_float(d[2]);
                        }
                    }
                    if (cmd == 0x64000000) {  // V2-32: UV
                        for (uint32_t k = 0; k < num; ++k, d += 2, ++uv) {
                            if (uv >= vert_count) {
                                ++g_shard_guards.vert_overflow;
                                continue;
                            }
                            verts[uv].uv[0] = word_float(d[0]);
                            verts[uv].uv[1] = word_float(d[1]);
                        }
                    }
                    if (cmd == 0x6e000000) {
                        if (((code >> 14) & 1) == 0) {
                            // V4-8 com sinal (normais): o bit 31 da palavra é o ADC do vértice.
                            for (uint32_t k = 0; k < num; ++k) {
                                if (int32_t(d[k]) < 0 || k < 2) continue;
                                if (tris >= tri_count) {
                                    ++g_shard_guards.tri_overflow;
                                    continue;
                                }
                                tri[tris].vert[0] = uint16_t(k - 2);
                                tri[tris].vert[1] = uint16_t(k - 1);
                                tri[tris].vert[2] = uint16_t(k);
                                tri[tris].texture = texture;
                                ++tris;
                            }
                        } else {
                            // V4-8 sem sinal: a cor, um float por byte (escala do GS).
                            for (uint32_t k = 0; k < num; ++k, ++colour) {
                                if (colour >= vert_count) {
                                    ++g_shard_guards.vert_overflow;
                                    continue;
                                }
                                const uint32_t w = d[k];
                                verts[colour].rgba[0] = float(w & 0xff);
                                verts[colour].rgba[1] = float((w >> 8) & 0xff);
                                verts[colour].rgba[2] = float((w >> 16) & 0xff);
                                verts[colour].rgba[3] = float(w >> 24);
                            }
                        }
                    }
                    p += words;
                } else {
                    ++g_shard_guards.unknown_code;
                    return;
                }
            }
            tag = *p;
        } else if ((tag & 0xf0000000) == 0x30000000) {  // REF: a textura dos lotes seguintes
            texture = p[1];
            p += 4;
            tag = *p;
        } else {
            ++g_shard_guards.unknown_code;
            return;
        }
    }
}

// [decomp] 0x0011c9d8 psiGetTriList
// Os triângulos e vértices guardados por psiCreateBreakableData, com as quantidades da caixa 0 (zero
// se o modelo não tem os dados). O registro é alocado a cada chamada e nunca liberado (o heap
// recomeça na próxima fase).
// Divergência: objeto sem modelo, sem bloco 0x2d ou sem o registro de 0x18 bytes (o original leria
// endereços baixos) devolve quantidades zero.
TRILIST_tag* psiGetTriList(obj_tag* obj) {
    auto* list = static_cast<TRILIST_tag*>(Mem_Malloc(0x10, 0x904, 0));
    if (list == nullptr) return nullptr;
    const uint32_t* hdr = gfx_header(obj != nullptr ? obj->glist : nullptr);
    const auto* data = hdr != nullptr ? reinterpret_cast<const BREAKGFX_tag*>(ptr_word(hdr[2])) : nullptr;
    list->tris = data != nullptr ? data->tris : nullptr;
    list->verts = data != nullptr ? data->verts : nullptr;
    if (list->tris == nullptr || list->verts == nullptr || hdr[1] == 0) {
        list->tri_count = 0;
        list->vert_count = 0;
    } else {
        list->tri_count = int32_t(box0_word(hdr, 0x30));
        list->vert_count = int32_t(box0_word(hdr, 0x2c));
    }
    return list;
}

// [decomp] 0x001178e0 psiCreateShard
// A cadeia de DMA de um estilhaço (0x120 bytes no heap): REF para o pacote da textura; uma CNT com
// STCYCL 4,4, uma GIF tag de leque (5 laços) e MSCNT; a GIF tag da tira (3 vértices, PRIM 0x5c:
// Gouraud, textura, mistura; w = 8, a rotina comum), os três vértices (posição, cor como inteiro,
// zero), as UVs (u, v, 1) em 0x3a8 e outro MSCNT; RET. A cor float vira inteiro por truncamento.
// Igual no PC (psiDrawShard lê os vértices daqui).
int32_t* psiCreateShard(_SHATTER_VERT_LIST* list) {
    auto* start = static_cast<uint32_t*>(Mem_Malloc(kShardChainBytes, 0x2810, 0));
    if (start == nullptr) return nullptr;
    uint32_t* w = start;
    w[0] = 0x30000004;
    w[1] = list->vert[0].texture;
    w[2] = 0;
    w[3] = 0;
    uint32_t* cnt = w + 4;
    w[4] = 0x10000000;
    w[5] = 0;
    w[6] = 0x01000404;
    w[7] = 0x6c0102e1;
    w[8] = 0x8005;
    w[9] = 0x302ec000;
    w[10] = 0x412;
    w[11] = 0;
    w[12] = 0x17000000;
    w[13] = 0x6c0a02e1;
    w[14] = 0x8003;
    w[15] = 0x302e4000;
    w[16] = 0x412;
    w[17] = 8;
    uint32_t* d = w + kShardVertWord;
    for (const SHATTER_VERT& v : list->vert) {
        d[0] = float_word(v.pos.x);
        d[1] = float_word(v.pos.y);
        d[2] = float_word(v.pos.z);
        d[3] = 0;
        for (int c = 0; c < 4; ++c) d[4 + c] = uint32_t(int32_t(v.rgba[c]));
        d[8] = d[9] = d[10] = d[11] = 0;
        d += 12;
    }
    *d++ = 0x6c0303a8;
    for (const SHATTER_VERT& v : list->vert) {
        d[0] = float_word(v.uv[0]);
        d[1] = float_word(v.uv[1]);
        d[2] = 0x3f800000;
        d[3] = 0;
        d += 4;
    }
    *d++ = 0x17000000;
    while ((reinterpret_cast<uintptr_t>(d) & 0xf) != 0) *d++ = 0;
    *cnt |= uint32_t(int32_t(reinterpret_cast<uint8_t*>(d) - reinterpret_cast<uint8_t*>(cnt)) / 16 - 1);
    *d = 0x60000000;
    do {
        ++d;
        if ((reinterpret_cast<uintptr_t>(d) & 0xf) == 0) break;
        *d = 0;
    } while (true);
    CheckSizeAndShrink(reinterpret_cast<int32_t*>(start), reinterpret_cast<int32_t*>(d), kShardChainBytes);
    return reinterpret_cast<int32_t*>(start);
}

// [decomp] 0x0011ca60 psiDeleteShard
// No PS2 põe o ponteiro na lista de liberação do buffer de DMA do quadro (AddChainFreePtr), que
// PS2StartCalcPacket libera dois quadros depois, quando o GS já não lê a cadeia. Mas UpdateAllShards
// passa o endereço do campo SHARD_tag::chain, não a cadeia, e libera o nó logo em seguida: a lista
// liberaria o meio de um bloco já livre (corromperia o heap) e a cadeia nunca seria liberada.
// Divergência: no PC o desenho já foi copiado para o registro do quadro, então a cadeia (*chain_field)
// é liberada na hora e o campo zerado.
void psiDeleteShard(P32<int32_t>* chain_field) {
    if (chain_field == nullptr) return;
    Mem_Free(chain_field);
}

// [decomp] 0x0011ca88 psiFinishedObjectShatter
// Vazia no PS2.
void psiFinishedObjectShatter() {}

// [decomp] 0x00121388 psiSetShardRenderStates
// Estado dos estilhaços: Z testado (GEQUAL, alfa > 0) sem escrita, mistura transparente, tweak branco;
// a próxima textura de estilhaço vai ao GS.
void psiSetShardRenderStates() {
    psiSwitchZBufferRead(0);
    psiSwitchZBufferWrite(1);
    psiSetUpColourBlend(1);
    psiSetTweakARGB(0xff, 0xff, 0xff, 0xff);
    ShardTexture = 0xffffffff;
}

// [decomp] 0x0011f420 GetTextureFromDMA
// O registro (pstex_tag*) da entrada de Tex cujo pacote de registradores é dma; 0 se nenhuma.
uint32_t GetTextureFromDMA(int32_t* dma) {
    for (int32_t i = 0; i < NumPS2TexturesLoaded; ++i) {
        if (reinterpret_cast<int32_t*>(nf::raw(Tex[i].dma)) == dma) return nf::p32_u(Tex[i].rec);
    }
    return 0;
}

// [decomp] 0x00120880 psiFont_SetTexture
// No PS2: põe a textura no cache da VRAM (CacheTextureSpace, CacheTexture, trocando o buffer do cache
// se não couber) e guarda o endereço e as medidas dela para os próximos desenhos. No PC as texturas
// já estão na GPU: guarda o índice de Tex (pstex_tag::tex_index) em g_psi_font_texture.
void psiFont_SetTexture(uint32_t texture) {
    if (texture == 0) return;
    g_psi_font_texture = nf::p32_ptr<const pstex_tag>(texture)->tex_index;
}

// [decomp] 0x00122ae8 psiDrawShard
// No PS2: a coluna w da matriz vira (0, 0, 0, 1); se a textura mudou, GetTextureFromDMA e
// psiFont_SetTexture a põem no GS; então um pacote de 24 quadwords para o endereço 0 da VU1 (matriz
// objeto -> tela e de recorte, os buffers de lote e a rotina 0, sem luzes nem tweak) e um CALL para a
// cadeia do estilhaço.
// No PC: a mesma parte de estado; os três vértices da cadeia vão ao mundo pela matriz (vetor-linha) e
// viram um triângulo do registro do quadro, com a cor da cadeia (escala do GS) e a textura em vigor.
void psiDrawShard(int32_t* chain, MATRIX* m, uint32_t texture) {
    m->m[3][3] = 1.0f;
    m->m[0][3] = 0.0f;
    m->m[1][3] = 0.0f;
    m->m[2][3] = 0.0f;
    if (texture != ShardTexture) {
        psiFont_SetTexture(GetTextureFromDMA(nf::p32_ptr<int32_t>(texture)));
        ShardTexture = texture;
    }
    if (chain == nullptr) return;
    const auto* w = reinterpret_cast<const uint32_t*>(chain);
    PsiTriVertex v[3];
    for (int k = 0; k < 3; ++k) {
        const uint32_t* d = w + kShardVertWord + k * 12;
        const float p[3] = {word_float(d[0]), word_float(d[1]), word_float(d[2])};
        for (int c = 0; c < 3; ++c) {
            v[k].pos[c] = p[0] * m->m[0][c] + p[1] * m->m[1][c] + p[2] * m->m[2][c] + m->m[3][c];
        }
        for (int c = 0; c < 4; ++c) v[k].rgba[c] = uint8_t(d[4 + c]);
        const uint32_t* t = w + kShardUvWord + k * 4;
        v[k].uv[0] = word_float(t[0]);
        v[k].uv[1] = word_float(t[1]);
    }
    psi_draw_triangles(v, 3, g_psi_font_texture);
}
