// O GeoPrimState, o RStateManager e o TAR (eagl_state.h), portados do DRIVING.ELF.

#include "driving/eagl_state.h"

#include <cctype>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>

namespace drv::eagl {

int runtime_alloc_unported = 0;

namespace {

uint64_t& q(uint8_t* s, int off) { return *reinterpret_cast<uint64_t*>(s + off); }

// DAT_003483b0: o tipo de primitiva do EAGL -> o PRIM do GS (-1: não suportado).
const int8_t kPrimMap[11] = {0, 1, 2, -1, 3, 4, 5, -1, -1, -1, 6};
// DAT_003483c8: o teste de Z do EAGL -> o ZTST do GS (o Z do PS2 cresce para perto: "gequal" é o normal).
const int kDepthMap[8] = {0, 1, -1, -1, -1, -1, 2, 3};

struct Value {
    const char* key;
    int value;
};
// FUN_001cd3b8: a entrada com a chave igual (a função devolve a entrada; quem chama lê o valor).
const Value* lookup(const char* s, const Value* table) {
    for (const Value* v = table; v->key; ++v)
        if (std::strcmp(s, v->key) == 0) return v;
    return nullptr;
}

const Value kPrimTypes[] = {{"pointlist", 0}, {"linelist", 1}, {"lineloop", 3}, {"trilist", 4}, {"tristrip", 5},
                            {"trifan", 6},    {"quadlist", 7}, {"quadstrip", 8}, {"polygon", 9}, {"sprite", 10},
                            {nullptr, 0}};
const Value kShading[] = {{"flat", 0}, {"gouraud", 1}, {"specular", 2}, {nullptr, 0}};
const Value kOnOff[] = {{"off", 0}, {"on", 1}, {nullptr, 0}};
const Value kTexcoord[] = {{"stq", 0}, {"uv", 1}, {nullptr, 0}};
const Value kDepthTest[] = {{"never", 0}, {"always", 1}, {"notequal", 2}, {"less", 3}, {"lequal", 4},
                            {"equal", 5}, {"gequal", 6}, {"greater", 7}, {nullptr, 0}};
const Value kAlphaTest[] = {{"never", 0}, {"always", 1}, {"notequal", 7}, {"less", 2}, {"lequal", 3},
                            {"equal", 4}, {"gequal", 5}, {"greater", 6}, {nullptr, 0}};
const Value kTransparency[] = {{"opaque", 0}, {"alpha", 1}, {"chroma", 2}, {nullptr, 0}};
const Value kAlphaBm[] = {{"off", 0}, {"blend", 1}, {"add", 2}, {"atten", 3}, {"mod", 4},
                          {"sub", 5}, {"custom", 6}, {nullptr, 0}};
const Value kTexAlpha[] = {{"noA", 0}, {"deepA", 1}, {"1bitA", 0x70}, {nullptr, 0}};
const Value kMaps[] = {{"chromemap", 0}, {"glossmap", 1}, {"envmap", 2}, {"specmap", 3}, {"watermap", 4},
                       {"glassdmgmap", 5}, {"bumpmap", 6}, {"shadowmap", 7}, {"glaregen", 8}, {nullptr, 0}};

constexpr uint64_t PRIM_IIP = 0x08, PRIM_TME = 0x10, PRIM_FGE = 0x20, PRIM_ABE = 0x40, PRIM_AA1 = 0x80;
constexpr uint64_t ALPHA_BLEND = 0x8000000044ull;  // (Cs - Cd) * As + Cd

// Os tratadores das chaves "modify=" (0x387250) e "name=" (0x3872a0). O primeiro devolve 1 só em specmap.
int modify_handler(int which, uint8_t* s) {
    switch (which) {
    case 3:  // specmap (0x001cdd18)
        q(s, 0x10) &= ~PRIM_ABE;
        q(s, 0x30) &= ~1ull;
        return 1;
    case 7:  // shadowmap (0x001cdd80)
        q(s, 0x10) = (q(s, 0x10) & ~0x78ull) | 0x58;
        q(s, 0x30) = (q(s, 0x30) & ~0x3FFFull) | 0x315D;
        q(s, 0x40) = ALPHA_BLEND;
        return 0;
    case 8:  // glaregen (0x001cdd58)
        q(s, 0x10) &= ~PRIM_ABE;
        q(s, 0x30) &= ~1ull;
        return 0;
    default:  // chromemap, glossmap, envmap, watermap, glassdmgmap, bumpmap: nada
        return 0;
    }
}

void name_handler(int which, uint8_t* s) {
    switch (which) {
    case 1:  // glossmap (0x001cd9c0)
        q(s, 0x10) = (q(s, 0x10) & ~0x58ull) | 0x18;
        GeoPrimState_SetDepthTest(s, 4);  // "lequal": recusado, o Z fica como está
        q(s, 0x30) &= ~1ull;
        break;
    case 2:  // envmap (0x001cda30): Cs * As + Cd, sem névoa, com o teste de alfa (que impede gravar o Z)
        q(s, 0x40) = 0x8000000048ull;
        q(s, 0x10) = (q(s, 0x10) & ~0x78ull) | 0x58;
        GeoPrimState_SetDepthTest(s, 5);  // "equal": recusado
        q(s, 0x30) = (q(s, 0x30) & ~1ull) | 1;
        break;
    case 3:  // specmap (0x001cdad0): Cs * Ad + Cd (o alfa do quadro), sem névoa, sem teste de alfa
        q(s, 0x40) = 0x8000000058ull;
        q(s, 0x10) = (q(s, 0x10) & ~0x78ull) | 0x58;
        GeoPrimState_SetDepthTest(s, 5);
        q(s, 0x30) &= ~1ull;
        break;
    case 4:  // watermap (0x001cdb70)
        q(s, 0x70) = (q(s, 0x70) & ~1ull) | 1;
        q(s, 0x30) &= ~1ull;
        q(s, 0x10) = (q(s, 0x10) & ~0x58ull) | 0x58;
        q(s, 0x40) = ALPHA_BLEND;
        q(s, 0x28) = (q(s, 0x28) & ~0xF0000ull) | 0xE0000;  // o FBA_1 passa a ser escrito (A+D)
        break;
    case 5:  // glassdmgmap (0x001cdc08)
        q(s, 0x30) = (q(s, 0x30) & ~1ull) | 1;
        q(s, 0x10) = (q(s, 0x10) & ~0x58ull) | 0x58;
        q(s, 0x40) = ALPHA_BLEND;
        break;
    default:  // bumpmap (0x001cdc78) não faz nada; shadowmap e chromemap não têm tratador
        break;
    }
}

// Os tratadores das chaves (a tabela 0x333f58). `next` é o resto da especificação: o tratador de texalpha lê o s1 de
// quem o chama, que é esse ponteiro (nunca nulo), e por isso nunca chega à parte que liga a mistura (ver abaixo).
int apply_key(const char* key, const char* value, uint8_t* s, const char* next) {
    const Value* v = nullptr;
    if (!std::strcmp(key, "modify")) {
        if ((v = lookup(value, kMaps))) return modify_handler(v->value, s);
    } else if (!std::strcmp(key, "primtype")) {
        if ((v = lookup(value, kPrimTypes))) GeoPrimState_SetPrimType(s, v->value);
    } else if (!std::strcmp(key, "shading")) {
        if ((v = lookup(value, kShading)) && v->value != 2)
            q(s, 0x10) = (q(s, 0x10) & ~PRIM_IIP) | uint64_t(v->value) << 3;
    } else if (!std::strcmp(key, "cull")) {
        lookup(value, kOnOff);  // o GS não descarta faces: a chave não muda o estado
    } else if (!std::strcmp(key, "texture")) {
        if ((v = lookup(value, kOnOff))) q(s, 0x10) = (q(s, 0x10) & ~PRIM_TME) | uint64_t(v->value != 0) << 4;
    } else if (!std::strcmp(key, "texcoord")) {
        if ((v = lookup(value, kTexcoord))) q(s, 0x10) = (q(s, 0x10) & ~0x100ull) | uint64_t(v->value) << 8;
    } else if (!std::strcmp(key, "depthtest")) {
        if ((v = lookup(value, kDepthTest))) GeoPrimState_SetDepthTest(s, v->value);
    } else if (!std::strcmp(key, "alphatestmethod")) {
        if ((v = lookup(value, kAlphaTest))) q(s, 0x30) = (q(s, 0x30) & ~0xEull) | uint64_t(v->value) << 1;
    } else if (!std::strcmp(key, "alphabm")) {
        if ((v = lookup(value, kAlphaBm))) {
            static const uint64_t eq[] = {0, 0x8000000044ull, 0x8000000048ull, 0x8000000088ull, 0, 0x8000000062ull};
            if (v->value == 0) q(s, 0x10) &= ~PRIM_ABE;
            else if (v->value <= 5 && v->value != 4) {
                q(s, 0x40) = eq[v->value];
                q(s, 0x10) |= PRIM_ABE;
            }
        }
    } else if (!std::strcmp(key, "transparency")) {
        if ((v = lookup(value, kTransparency)) && v->value != 2)
            q(s, 0x10) = (q(s, 0x10) & ~PRIM_ABE) | uint64_t(v->value) << 6;
    } else if (!std::strcmp(key, "chroma")) {
        // sscanf("0x%08x") sem efeito
    } else if (!std::strcmp(key, "alphatest")) {
        if ((v = lookup(value, kOnOff))) q(s, 0x30) = (q(s, 0x30) & ~1ull) | uint64_t(v->value != 0);
    } else if (!std::strcmp(key, "texalpha")) {
        // 0x001cd850: só com o teste de alfa ligado. noA desliga a mistura e o teste; deepA (1) e 1bitA (0x70) fazem
        // AREF = valor e AFAIL = "só RGB" e ligam a mistura. O resto do código (ALPHA = mistura) depende do s1 de
        // quem chama, sempre o resto da especificação (não nulo): nunca roda, como no PS2.
        v = lookup(value, kTexAlpha);
        if (v && (q(s, 0x30) & 1)) {
            if (v->value == 0) {
                q(s, 0x30) &= ~1ull;
                q(s, 0x10) &= ~PRIM_ABE;
            } else {
                q(s, 0x30) = (((q(s, 0x30) & ~0xFF0ull) | uint64_t(v->value) << 4) & ~0x3000ull) | 0x3000;
                q(s, 0x10) = (q(s, 0x10) & ~PRIM_ABE) | PRIM_ABE;
                if (!next) {
                    q(s, 0x40) = ALPHA_BLEND;
                    q(s, 0x10) |= PRIM_ABE;
                }
            }
        }
    } else if (!std::strcmp(key, "fog")) {
        if ((v = lookup(value, kOnOff))) q(s, 0x10) = (q(s, 0x10) & ~PRIM_FGE) | uint64_t(v->value != 0) << 5;
    } else if (!std::strcmp(key, "antialias")) {
        // 0x001cde80: qualquer valor achado na tabela (até "off") liga o AA1 e a mistura; um valor desconhecido desliga.
        if (lookup(value, kOnOff)) {
            q(s, 0x40) = ALPHA_BLEND;
            q(s, 0x10) = (q(s, 0x10) & ~0xC0ull) | (PRIM_AA1 | PRIM_ABE);
        } else {
            q(s, 0x10) &= ~PRIM_AA1;
        }
    } else if (!std::strcmp(key, "name")) {
        if ((v = lookup(value, kMaps))) name_handler(v->value, s);
    }
    return 0;
}

// [driving] 0x001ce3b0 RStateManager::ParseFirstStateTag
const char* ParseFirstStateTag(const char* p, uint8_t* state, int& ret) {
    ret = 0;
    if (!p) return nullptr;
    while (*p && !std::isalnum(static_cast<unsigned char>(*p))) ++p;  // a tabela de tipos do REAL: bits 0-2
    const char* k = p;
    while (*p && *p != '=') ++p;
    size_t klen = size_t(p - k);
    if (klen > 0x1f) klen = 0x1f;
    if (*p) ++p;
    if (klen == 0) return nullptr;
    const char* v = p;
    while (*p && *p != ';') ++p;
    size_t vlen = size_t(p - v);
    if (vlen > 0x3f) vlen = 0x3f;
    if (*p) ++p;
    char key[32], value[64];
    std::memcpy(key, k, klen);
    key[klen] = 0;
    std::memcpy(value, v, vlen);
    value[vlen] = 0;
    ret = apply_key(key, value, state, p);
    return p;
}

struct StateStore {
    std::deque<std::unique_ptr<uint8_t[]>> blocks;
    std::map<std::string, uint8_t*> by_content;
};
StateStore& store() {
    static StateStore s;
    return s;
}

}  // namespace

// [driving] 0x00283568 EAGL::GeoPrimState::GeoPrimState
void GeoPrimState_Init(uint8_t* s) {
    std::memset(s, 0, kGeoPrimStateSize);
    q(s, 0x00) = 0x1000000000000001ull;  // GIF tag: 1 laço, 1 registrador (PRIM)
    q(s, 0x08) = 0;
    q(s, 0x20) = 0x5000000000008001ull;  // GIF tag: 1 laço, fim, 5 registradores A+D
    q(s, 0x28) = 0xFFFEEull;             // A+D x5 (os 0xf seguintes nunca são lidos)
    q(s, 0x38) = 0x47;                   // TEST_1
    q(s, 0x48) = 0x42;                   // ALPHA_1
    q(s, 0x58) = 0x4C;                   // FRAME_1
    q(s, 0x68) = 0x4E;                   // ZBUF_1
    q(s, 0x78) = 0x4A;                   // FBA_1
    q(s, 0x30) = (q(s, 0x30) & ~(1ull << 16)) | 0x10000;  // ZTE
    q(s, 0x30) = (q(s, 0x30) & ~(3ull << 17)) | 0x40000;  // ZTST = 2 (GEQUAL)
    q(s, 0x30) = (q(s, 0x30) & ~(7ull << 1)) | 2;         // ATST = 1 (ALWAYS)
    q(s, 0x40) = 0x80000000A8ull;                          // Cs * FIX(0x80): sem mistura
    q(s, 0x10) = (q(s, 0x10) & ~0x7Full) | 8;              // IIP (Gouraud)
}

// [driving] 0x00283fc8 EAGL::GeoPrimState::SetPrimType
bool GeoPrimState_SetPrimType(uint8_t* s, int type) {
    if (type < 0 || type > 10 || kPrimMap[type] < 0) return false;
    q(s, 0x10) = uint64_t(kPrimMap[type]) | (q(s, 0x10) & ~7ull);
    return true;
}

// [driving] 0x00284038 EAGL::GeoPrimState::SetDepthTest
bool GeoPrimState_SetDepthTest(uint8_t* s, int test) {
    if (test < 0 || test > 7 || kDepthMap[test] < 0) return false;
    q(s, 0x30) = (q(s, 0x30) & ~(3ull << 17)) | uint64_t(kDepthMap[test]) << 17;
    return true;
}

// [driving] 0x001ce158 RStateManager::GetGeoPrimState
uint8_t* RStateManager_GetGeoPrimState(const char* spec) {
    alignas(16) uint8_t s[kGeoPrimStateSize];
    GeoPrimState_Init(s);
    std::memset(s, 0, sizeof s);
    GeoPrimState_Init(s);
    GeoPrimState_SetPrimType(s, 4);
    q(s, 0x30) = (q(s, 0x30) & ~0x3FFFull) | 0x3A0D;  // ATE, ATST GREATER, AREF 0xa0, AFAIL "só RGB"
    q(s, 0x10) = (q(s, 0x10) & ~0x70ull) | 0x70;      // TME, FGE, ABE
    q(s, 0x40) = ALPHA_BLEND;
    // Primeira passada: todas as chaves; guarda as que devolveram 1 para aplicar de novo no fim.
    const char* again[64];
    int n = 0;
    for (const char* p = spec; p;) {
        if (n < 64) again[n] = p;
        int ret = 0;
        p = ParseFirstStateTag(p, s, ret);
        if (ret && n < 63) ++n;
    }
    for (int i = 0; i < n; ++i) {
        int ret = 0;
        ParseFirstStateTag(again[i], s, ret);
    }
    const std::string key(reinterpret_cast<const char*>(s), sizeof s);
    StateStore& st = store();
    auto it = st.by_content.find(key);
    if (it != st.by_content.end()) return it->second;
    auto block = std::make_unique<uint8_t[]>(kGeoPrimStateSize + 15);
    uint8_t* aligned = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(block.get()) + 15) & ~uintptr_t(15));
    std::memcpy(aligned, s, sizeof s);
    st.blocks.push_back(std::move(block));
    st.by_content[key] = aligned;
    return aligned;
}

void RStateManager_Reset() {
    store().blocks.clear();
    store().by_content.clear();
}

int RStateManager_Count() { return int(store().by_content.size()); }

// ---------------------------------------------------------------- TAR

namespace {

std::deque<std::unique_ptr<TarImage>>& images() {
    static std::deque<std::unique_ptr<TarImage>> v;
    return v;
}
ShapeLookup g_shape_lookup = nullptr;
void* g_shape_user = nullptr;

int log2_ceil(int v) {
    int l = 0;
    while ((1 << l) < v) ++l;
    return l;
}

std::deque<std::unique_ptr<uint8_t[]>>& made_tars() {
    static std::deque<std::unique_ptr<uint8_t[]>> v;
    return v;
}

}  // namespace

TarImage* tar_image_create(uint16_t texture_id, int width, int height, uint8_t gs_psm, bool palette) {
    auto im = std::make_unique<TarImage>();
    std::memset(im->raw, 0, sizeof im->raw);
    uint8_t* r = im->raw;
    *reinterpret_cast<uint32_t*>(r + 0x10) = 1u | (palette ? 8u : 0u);  // um nível de mipmap; 8 = tem paleta
    *reinterpret_cast<uint16_t*>(r + 0x34) = texture_id;                 // TBP0: no PC, o índice da textura
    r[0x3b] = uint8_t((width + 63) / 64);                                // TBW
    r[0x3c] = gs_psm;
    *reinterpret_cast<uint32_t*>(r + 0x38) |= uint32_t(log2_ceil(width)) << 16 | uint32_t(log2_ceil(height)) << 20;
    *reinterpret_cast<uint32_t*>(r + 0x40) = uint32_t(width & 0xFFF) | uint32_t(height & 0xFFF) << 12;
    TarImage* p = im.get();
    images().push_back(std::move(im));
    return p;
}

void tar_images_reset() {
    images().clear();
    made_tars().clear();
}

void set_shape_lookup(ShapeLookup fn, void* user) {
    g_shape_lookup = fn;
    g_shape_user = user;
}

// [driving] 0x0027c6a0 EAGL::TAR::TAR
// Portados: a faixa de UV e os modos de repetição no CLAMP_1, o modo da textura (TFX). Só do port: a shape é achada
// por `set_shape_lookup` (o jogo procura a variável "shape_<nome>" que o RegisterShapes registrou) e o "anexar a
// imagem" (0x0027e1e8 / 0x0027cd68, a gerência de VRAM e dos mipmaps) só guarda o ponteiro para os dados da imagem.
// As faixas diferentes de [0,1] com repetição (REGION_REPEAT, 0x0027eb30/0x0027eb98) não foram portadas.
void TAR_Construct(uint32_t tar) {
    uint8_t* t = ptr_at(tar);
    float umin, umax, vmin, vmax;
    std::memcpy(&umin, t + 0x08, 4);
    std::memcpy(&umax, t + 0x0c, 4);
    std::memcpy(&vmin, t + 0x10, 4);
    std::memcpy(&vmax, t + 0x14, 4);
    const int32_t mode = *reinterpret_cast<int32_t*>(t + 0x18);
    const int32_t urepeat = *reinterpret_cast<int32_t*>(t + 0x1c);
    const int32_t vrepeat = *reinterpret_cast<int32_t*>(t + 0x20);
    char name[4];
    std::memcpy(name, t + 4, 4);
    TarImage* im = g_shape_lookup ? g_shape_lookup(name, g_shape_user) : nullptr;
    if (!im) im = tar_image_create(0xFFFF, 8, 8, 0, false);  // o padrão do EAGL (DAT_00344204): sem textura
    *reinterpret_cast<uint32_t*>(t) = addr_of(im);
    const uint32_t wh = *reinterpret_cast<uint32_t*>(im->raw + 0x40);
    const float w = float(wh & 0xFFF), h = float(wh >> 12 & 0xFFF);
    uint32_t& flags = *reinterpret_cast<uint32_t*>(t + 0x10);
    uint32_t& minw = *reinterpret_cast<uint32_t*>(t + 0x14);
    uint32_t& maxw = *reinterpret_cast<uint32_t*>(t + 0x18);
    minw = (minw & 0xFFFFF800u) | (uint32_t(int(umin * w)) & 0x7FF);
    maxw = (maxw & 0xFFFFF800u) | (uint32_t(int(umax * w)) & 0x7FF);
    minw = (minw & 0xFFC007FFu) | (uint32_t(int(vmin * h)) & 0x7FF) << 11;
    maxw = (maxw & 0xFFC007FFu) | (uint32_t(int(vmax * h)) & 0x7FF) << 11;
    if (umin == 0.0f && umax == 1.0f) flags = (flags & ~3u) | (urepeat ? 0u : 1u);  // REPEAT / CLAMP
    else flags = (flags & ~3u) | (urepeat ? 3u : 2u);                              // REGION_REPEAT / REGION_CLAMP
    if (vmin == 0.0f && vmax == 1.0f) flags = (flags & ~0xCu) | (vrepeat ? 0u : 4u);
    else flags = (flags & ~0xCu) | (vrepeat ? 0xCu : 8u);
    if (mode == 0 || mode == 2) flags &= 0xF9FFFFFFu;                      // TFX = MODULATE
    else if (mode == 1) flags = (flags & 0xF9FFFFFFu) | 0x2000000u;        // TFX = DECAL
}

// [driving] 0x0027d340 EAGL::TAR::BuildPacket
uint64_t* TAR_WritePacket(uint32_t tar4, uint64_t* out) {
    const uint32_t* p = ptr_at<const uint32_t>(tar4);  // o TAR + 4: p[3] = +0x10, p[4] = +0x14, p[5] = +0x18
    const uint8_t* im = ptr_at<const uint8_t>(*ptr_at<const uint32_t>(tar4 - 4));
    auto u8 = [&](int o) { return uint64_t(im[o]); };
    auto s16 = [&](int o) { return uint64_t(int64_t(*reinterpret_cast<const int16_t*>(im + o))) & 0xFFFFFFFFFFFFull; };
    auto u32 = [&](int o) { return *reinterpret_cast<const uint32_t*>(im + o); };
    const uint64_t f = p[3];
    out[0] = 0x6000000000008001ull;  // GIF tag: 1 laço, fim, 6 registradores
    out[1] = 0xEEEEEE;               // todos A+D
    out[2] = im[0x3c] == 1 ? 0x8080ull : 0x8000008000ull;  // TEXA
    out[3] = 0x3b;
    out[4] = (f & 3) | ((f >> 2) & 3) << 2 | (uint64_t(p[4]) & 0x7FF) << 4 | (uint64_t(p[5]) & 0x7FF) << 14 |
             (uint64_t(p[4] >> 11) & 0x7FF) << 24 | (uint64_t(p[5] >> 11) & 0x7FF) << 34;  // CLAMP_1
    out[5] = 8;
    out[6] = ((f >> 9) & 1) | uint64_t((u32(0x10) & 7) - 1) << 2 | ((f >> 4) & 3) << 5 | ((f >> 6) & 7) << 6 |
             ((f >> 10) & 3) << 19 | ((f >> 12) & 0xFFF) << 32;  // TEX1_1
    out[7] = 0x14;
    out[8] = (s16(0x34) & 0x3FFF) | u8(0x3b) << 14 | u8(0x3c) << 20 | uint64_t((u32(0x38) >> 16) & 0xF) << 26 |
             uint64_t((u32(0x38) >> 20) & 0xF) << 30 | ((f >> 24) & 1) << 34 | ((f >> 25) & 3) << 35 |
             (s16(0x1c) & 0x3FFF) << 37 | u8(0x24) << 51 | 0x2000000000000000ull;  // TEX0_1
    out[9] = 6;
    out[10] = s16(0x4c) | u8(0x53) << 14 | s16(0x64) << 20 | u8(0x6b) << 34 | s16(0x7c) << 40 | u8(0x83) << 54;  // MIPTBP1
    out[11] = 0x34;
    out[12] = s16(0x94) | u8(0x9b) << 14 | s16(0xac) << 20 | u8(0xb3) << 34 | s16(0xc4) << 40 | u8(0xcb) << 54;  // MIPTBP2
    out[13] = 0x36;
    return out + 14;
}

uint32_t tar_create(const char name[4], bool repeat_u, bool repeat_v) {
    auto block = std::make_unique<uint8_t[]>(0x60 + 15);
    uint8_t* t = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(block.get()) + 15) & ~uintptr_t(15));
    std::memset(t, 0, 0x60);
    std::memcpy(t + 4, name, 4);
    const float one = 1.0f;
    std::memcpy(t + 0x0c, &one, 4);
    std::memcpy(t + 0x14, &one, 4);
    *reinterpret_cast<int32_t*>(t + 0x1c) = repeat_u ? 1 : 0;
    *reinterpret_cast<int32_t*>(t + 0x20) = repeat_v ? 1 : 0;
    made_tars().push_back(std::move(block));
    TAR_Construct(addr_of(t));
    return addr_of(t);
}

void gs_read_packet(const uint8_t* qw, int max_qw, GsRegs& regs) {
    int i = 0;
    while (i < max_qw) {
        const uint64_t tag = *reinterpret_cast<const uint64_t*>(qw + i * 16);
        const uint64_t regs_desc = *reinterpret_cast<const uint64_t*>(qw + i * 16 + 8);
        ++i;
        const int nloop = int(tag & 0x7FFF);
        const bool eop = (tag >> 15) & 1;
        const int flg = int(tag >> 58) & 3;
        int nreg = int(tag >> 60) & 15;
        if (nreg == 0) nreg = 16;
        if (tag >> 46 & 1) {  // PRE: o PRIM do próprio tag
            regs.has_prim = true;
            regs.prim = (tag >> 47) & 0x7FF;
        }
        if (flg != 0) {  // REGLIST ou IMAGE: não aparecem nos pacotes de estado
            if (eop) break;
            continue;
        }
        for (int l = 0; l < nloop && i < max_qw; ++l)
            for (int r = 0; r < nreg && i < max_qw; ++r, ++i) {
                const int reg = int(regs_desc >> (r * 4)) & 15;
                const uint64_t lo = *reinterpret_cast<const uint64_t*>(qw + i * 16);
                const uint64_t hi = *reinterpret_cast<const uint64_t*>(qw + i * 16 + 8);
                int addr = -1;
                uint64_t data = lo;
                if (reg == 0) addr = 0x00;       // PRIM
                else if (reg == 0xE) addr = int(hi & 0xFF);  // A+D
                switch (addr) {
                case 0x00: regs.has_prim = true; regs.prim = data & 0x7FF; break;
                case 0x06: regs.has_tex0 = true; regs.tex0 = data; break;
                case 0x08: regs.has_clamp = true; regs.clamp = data; break;
                case 0x14: regs.has_tex1 = true; regs.tex1 = data; break;
                case 0x3b: regs.texa = data; break;
                case 0x42: regs.has_alpha = true; regs.alpha = data; break;
                case 0x47: regs.has_test = true; regs.test = data; break;
                case 0x4e: regs.has_zbuf = true; regs.zbuf = data; break;
                default: break;
                }
            }
        if (eop) break;
    }
}

}  // namespace drv::eagl
