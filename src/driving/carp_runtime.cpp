// O CARP na memória como o jogo o usa (carp_runtime.h).
//
// Lido nas decompilações do PS2 (out/decomp_drv_carp e out/decomp_drv_ugroup do checkout principal); o nightfire-cxbx
// (NightfireResearch: src/driving/data/Carp.cpp, SymbolTable.cpp, UData.cpp, engine/UGroup.cpp) ajudou a ler a busca
// binária das etiquetas (FUN_002d4178) e o mapa dos resolvedores; os leiautes e os desvios são os do PS2.

#include "driving/carp_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace drv {

namespace {

CarpResolveStats g_stats;

uint8_t* addr(uint32_t a) { return nf::p32_ptr<uint8_t>(a); }
uint32_t word(const void* p) { return nf::p32_u(p); }

// FUN_002d4178 (o "lower_bound" das etiquetas, chamado por FUN_002d4268/FUN_002d4288): a busca binária num vetor
// ordenado de cabeçalhos; achando a etiqueta, volta ao primeiro registro com ela; senão para na posição de inserção
// (o primeiro maior). Devolve a etiqueta da posição final (0 no fim) e a posição em `*records`.
uint32_t find_tag(uint32_t count, UData** records, uint32_t tag) {
    if (count == 0) return 0;
    UData* first = *records;
    uint32_t n = count;
    do {
        const uint32_t half = n >> 1;
        UData* middle = &first[half];
        if (middle->tag > tag) {
            n = half;
        } else if (middle->tag == tag) {
            uint32_t h = half;
            while (h > 0 && first[h - 1].tag == tag) --h;
            *records = &first[h];
            return (*records)->tag;
        } else {
            first = middle + 1;
            n = n - half - 1;
        }
    } while (n > 0);
    uint32_t index = uint32_t(first - *records);
    if (index >= count) {
        *records = first;
        return 0;
    }
    if (first->tag < tag) ++index;
    *records = *records + index;
    if (index >= count) return 0;
    return (*records)->tag;
}

}  // namespace

UData* UGroup_GetArray(const UGroup* g) {
    const uint8_t* self = reinterpret_cast<const uint8_t*>(g);
    if (g->flags & 2) return reinterpret_cast<UData*>(const_cast<uint8_t*>(self) + size_t(g->offset) * 16);
    return reinterpret_cast<UData*>(addr(g->offset));
}

uint8_t* UData_Data(const UData* d) {
    const uint8_t* self = reinterpret_cast<const uint8_t*>(d);
    if (d->flags & 2) return const_cast<uint8_t*>(self) + d->offset;
    return addr(d->offset);
}

UGroup* UGroup_GroupLocateTag(const UGroup* g, uint32_t tag) {
    UData* groups = UGroup_GetArray(g);
    const uint32_t n = UGroup_GroupCount(g);
    if (g->flags & 0x10) {
        UData* found = groups;
        if (find_tag(n, &found, tag) == tag) return found;
        return groups + n;
    }
    UData* p = groups;
    while (p < groups + n && p->tag != tag) ++p;
    return p;
}

UData* UGroup_DataLocateTag(const UGroup* g, uint32_t tag) {
    UData* items = UGroup_GroupEnd(g);
    if (g->flags & 8) {
        UData* found = items;
        if (find_tag(g->count, &found, tag) == tag) return found;
        return UGroup_DataEnd(g);
    }
    UData* end = UGroup_DataEnd(g);
    UData* p = items;
    while (p < end && p->tag != tag) ++p;
    return p;
}

UData* UGroup_DataLocateFirst(const UGroup* g, uint32_t tag, int32_t index, int32_t max) {
    UData* found = UGroup_GroupEnd(g);
    if (index == -1) {
        if (find_tag(g->count, &found, tag) == tag) return found;
        return UGroup_DataEnd(g);
    }
    const uint32_t t = find_tag(g->count, &found, (tag & 0xFFFF0000u) | uint32_t(index));
    if (((t & 0xFFFF0000u) | 0x2020u) == tag && (t & 0xFFFF) <= uint32_t(max)) return found;
    return UGroup_DataEnd(g);
}

void UGroup_Deserialize(UGroup* g) {
    UData* array = UGroup_GetArray(g);
    g->offset = word(array);
    g->flags &= ~2u;
    for (uint32_t i = 0; i < UGroup_GroupCount(g); ++i) UGroup_Deserialize(array + i);
    UData* items = array + UGroup_GroupCount(g);
    for (uint32_t i = 0; i < g->count; ++i) {
        UData* d = items + i;  // [driving] 0x002d4930 UData::ResolveOffsets
        d->offset = word(UData_Data(d));
        d->flags &= ~2u;
    }
}

void UData_AdoptData(UData* d, uint32_t data, uint32_t size, uint32_t count, bool own) {
    // O bloco "próprio" (bit 2) seria liberado aqui; o CARP nunca adota um bloco próprio (só do port: nada a liberar).
    d->count = count;
    d->offset = data;
    d->flags = (d->flags & 0xF9u) | (own ? 4u : 0u) | (size << 8);
}

uint32_t UDataGroupDecodeTag(const char* text) {
    uint32_t tag = 0;
    const size_t n = std::strlen(text);
    if (n > 5 && text[0] == '{' && (text[5] == '}' || (n > 9 && text[9] == '}'))) {
        auto ch = [](char c) { return uint32_t(int32_t(int8_t(c))); };
        tag = ch(text[1]) << 24 | ch(text[2]) << 16 | ch(text[3]) << 8 | ch(text[4]);
        if (text[5] != '}') {
            unsigned number = 0xFFFFFFFFu;
            char hex[5] = {text[5], text[6], text[7], text[8], 0};
            char* end = nullptr;
            const unsigned long v = std::strtoul(hex, &end, 16);
            if (end && end != hex) number = unsigned(v);
            if (number != 0xFFFFFFFFu) tag = (tag & 0xFFFF0000u) | number;
        }
    }
    return tag;
}

// ---------------------------------------------------------------- a tabela de símbolos

void CarpSymbols::add_carp_file(UGroup* root) {
    groups_.emplace("<<Root>>", root);
    UGroup* map = UGroup_GroupLocateTag(root, 0x4d617020);  // 'Map '
    if (map != UGroup_GroupEnd(root)) groups_.emplace("<<Map>>", map);
    UGroup* shared = UGroup_GroupLocateTag(root, 0x53686172);  // 'Shar'
    if (shared != UGroup_GroupEnd(root)) groups_.emplace("<<Shared>>", shared);
    UData* g = UGroup_GetArray(root);
    for (uint32_t i = 0; i < UGroup_GroupCount(root); ++i) {
        if (UData_MatchTag(g + i) != 0x41727469) continue;  // 'Arti'
        UData* name = UGroup_DataLocateTag(g + i, 0x4e616d65);  // 'Name' (todo artigo tem)
        if (name == UGroup_DataEnd(g + i)) continue;
        groups_.emplace(reinterpret_cast<const char*>(UData_Data(name)), g + i);
    }
}

uint32_t CarpSymbols::carp_lookup(const char* name, uint32_t& size) const {
    // [driving] 0x002cdbd0 UCarpNamespace::ParseNameAndTag
    char group[96];
    uint32_t tag = 0;
    int offset = 0;
    const char* tag_text = name;
    if (*name == '{') {
        tag = UDataGroupDecodeTag(name);
        group[0] = 0;
    } else {
        std::snprintf(group, sizeof group, "%s", name);
        char* sep = std::strstr(group, "::");
        if (!sep) {
            tag = 0;
        } else {
            *sep = 0;
            tag_text = name + (sep - group) + 2;
            tag = UDataGroupDecodeTag(tag_text);
        }
    }
    if (tag) {
        const char* off = std::strstr(tag_text, "}::");
        if (off) offset = std::atoi(off + 3);
    }
    auto it = groups_.find(group);
    if (it == groups_.end()) return 0;
    UGroup* g = it->second;
    if (tag == 0) {
        size = 0;
        return word(g);
    }
    UData* rec = UGroup_DataLocateTag(g, tag);
    if (rec == UGroup_DataEnd(g) && (tag & 0xFFFF0000u) == 0x61730000u) rec = UGroup_DataLocateTag(g, tag & 0xFFFF00FFu);
    if (rec == UGroup_DataEnd(g)) return 0;
    size = UData_Size(rec);
    return word(UData_Data(rec) + offset);
}

uint32_t CarpSymbols::lookup(const char* name, uint32_t& size) {
    size = 0;
    const char* sep = std::strstr(name, "::");
    if (!sep) return 0;
    const std::string space(name, size_t(sep - name));
    const char* rest = sep + 2;
    uint32_t v = 0;
    if (space == "CARP") v = carp_lookup(rest, size);
    else if (space == "CHAR") {
        size = uint32_t(std::strlen(rest) + 1);
        v = word(rest);
    } else {
        auto it = spaces_.find(space);
        if (it != spaces_.end()) v = it->second(rest, size);
    }
    if (v) ++resolved;
    else misses[space]++;
    return v;
}

// ---------------------------------------------------------------- a resolução

void CARP_TagReference(uint32_t* ref, const UGroup* shared, const UGroup* local) {
    const uint32_t tag = *ref;
    if (tag == 0) return;
    if (tag == 0xFFFFFFFFu) {
        *ref = word(local);
        return;
    }
    UData* rec = UGroup_DataLocateTag(local, tag);
    if (rec == UGroup_DataEnd(local)) {
        rec = UGroup_DataLocateTag(shared, tag);
        if (rec == UGroup_DataEnd(shared)) {
            *ref = 0;
            return;
        }
    }
    *ref = word(UData_Data(rec));
}

namespace {

uint32_t* W(uint8_t* p, size_t off) { return reinterpret_cast<uint32_t*>(p + off); }

// Os resolvedores (CARP::InitResolvers 0x002da838: a etiqueta -> a função). `rec` é o registro, `shared` o grupo
// compartilhado, `parent` o grupo do registro. Os "if (p + k != 0)" do original (o new de colocação) não mudam nada
// aqui: os dados nunca estão no endereço 0.

// [driving] 0x002d9be0 (o resolvedor do 'Base' e do 'pD')
void resolve_base(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    *W(p, 0x34) = 0x4e616d65;  // 'Name'
    CARP_TagReference(W(p, 0), shared, parent);
    CARP_TagReference(W(p, 0x34), shared, parent);
    CARP_TagReference(W(p, 0xc), shared, parent);
}
// [driving] 0x002d9c78 (o 'as')
void resolve_as(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (int i = 0; i < 4; ++i) CARP_TagReference(W(p, size_t(i) * 4), shared, parent);
    for (uint32_t i = 0; i < p[0x1c]; ++i) CARP_TagReference(W(p, 0x24 + size_t(i) * 8), shared, parent);
}
// [driving] 0x002d9d58 (o 'an': elementos de 0x10, a referência em +0)
void resolve_an(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (uint32_t i = 0; i < rec->count; ++i) CARP_TagReference(W(p, size_t(i) * 0x10), shared, parent);
}
// [driving] 0x002d9de8 (o 'aq': elementos de 0x20, +0x1c)
void resolve_aq(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (uint32_t i = 0; i < rec->count; ++i) CARP_TagReference(W(p, size_t(i) * 0x20 + 0x1c), shared, parent);
}
// [driving] 0x002d9e78 (o 'el': cada evento pela função de RegisterEvent::ResolveEvent, FUN_001757d8). Os eventos não
// foram portados: o port só conta (os dados dos eventos ficam como estão no arquivo).
void resolve_el(UData* rec, UGroup*, UGroup*) {
    (void)rec;
    ++g_stats.event_lists;
}
// [driving] 0x002d9f18 (o 'ef': elementos de 0x40, +0x18 só no tipo 6)
void resolve_ef(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (uint32_t i = 0; i < rec->count; ++i)
        if (p[size_t(i) * 0x40 + 0x14] == 6) CARP_TagReference(W(p, size_t(i) * 0x40 + 0x18), shared, parent);
}
// [driving] 0x002d9fc0 (o 'di': +0xc..+0x20)
void resolve_di(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (int i = 0; i < 6; ++i) CARP_TagReference(W(p, 0xc + size_t(i) * 4), shared, parent);
}
// [driving] 0x002da088 (o 'in' e o 'Cams': elementos de 0x40, +0x1c)
void resolve_in(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (uint32_t i = 0; i < rec->count; ++i) CARP_TagReference(W(p, size_t(i) * 0x40 + 0x1c), shared, parent);
}
// [driving] 0x002da128 (o 'ps': elementos de 0x20; tipo 2: +0xc e +0x10)
void resolve_ps(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (uint32_t i = 0; i < rec->count; ++i) {
        uint8_t* e = p + size_t(i) * 0x20;
        if (e[0] == 2) {
            CARP_TagReference(W(e, 0xc), shared, parent);
            CARP_TagReference(W(e, 0x10), shared, parent);
        }
    }
}
// [driving] 0x002da208 (o 'ci': elementos de 0x40, +0x1c)
void resolve_ci(UData* rec, UGroup* shared, UGroup* parent) { resolve_in(rec, shared, parent); }
// [driving] 0x002da2a8 (o 'Trgr': elementos de 0x40, +0x18)
void resolve_trgr(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (uint32_t i = 0; i < rec->count; ++i) CARP_TagReference(W(p, size_t(i) * 0x40 + 0x18), shared, parent);
}
// [driving] 0x002da348 (o 'Rule': elementos de 0x20, +0xc e, nos tipos 2, 3, 4, 7, 8, 10, 12, +0x14)
void resolve_rule(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (uint32_t i = 0; i < rec->count; ++i) {
        uint8_t* e = p + size_t(i) * 0x20;
        CARP_TagReference(W(e, 0xc), shared, parent);
        switch (*W(e, 0)) {
            case 2: case 3: case 4: case 7: case 8: case 10: case 12:
                CARP_TagReference(W(e, 0x14), shared, parent);
                break;
            default:
                break;
        }
    }
}
// [driving] 0x002da428 (o 'AIEl': elementos de 0x100, +0xf8)
void resolve_aiel(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (uint32_t i = 0; i < rec->count; ++i) CARP_TagReference(W(p, size_t(i) * 0x100 + 0xf8), shared, parent);
}
// [driving] 0x002da4c8 (o 'AICo': elementos de 0xa0, +0x50 e +0x80)
void resolve_aico(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (uint32_t i = 0; i < rec->count; ++i) {
        CARP_TagReference(W(p, size_t(i) * 0xa0 + 0x50), shared, parent);
        CARP_TagReference(W(p, size_t(i) * 0xa0 + 0x80), shared, parent);
    }
}
// [driving] 0x002da590 (o 'AISp': elementos de 0x70, +0x40)
void resolve_aisp(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (uint32_t i = 0; i < rec->count; ++i) CARP_TagReference(W(p, size_t(i) * 0x70 + 0x40), shared, parent);
}
// [driving] 0x002da638 (o 'Wmap': +0..+0xc)
void resolve_wmap(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (int i = 0; i < 4; ++i) CARP_TagReference(W(p, size_t(i) * 4), shared, parent);
}
// [driving] 0x002da6d8 (o 'wn': quatro elementos de 0x10, +0, sem olhar a contagem)
void resolve_wn(UData* rec, UGroup* shared, UGroup* parent) {
    uint8_t* p = UData_Data(rec);
    for (int i = 0; i < 4; ++i) CARP_TagReference(W(p, size_t(i) * 0x10), shared, parent);
}

using Resolver = void (*)(UData*, UGroup*, UGroup*);

// [driving] 0x002da838 CARP::InitResolvers: o mapa das etiquetas (o 'MSet' aponta 0x002da630, o fim de outra função:
// um "jr ra" sem efeito; o 'AISp' é registrado duas vezes).
const std::map<uint32_t, Resolver>& resolvers() {
    static const std::map<uint32_t, Resolver> m = {
        {0x576d6170, resolve_wmap}, {0x776e2020, resolve_wn},   {0x696e2020, resolve_in},   {0x43616d73, resolve_in},
        {0x64692020, resolve_di},   {0x70732020, resolve_ps},   {0x63692020, resolve_ci},   {0x54726772, resolve_trgr},
        {0x52756c65, resolve_rule}, {0x4149456c, resolve_aiel}, {0x41495370, resolve_aisp}, {0x4d536574, nullptr},
        {0x4149436f, resolve_aico}, {0x42617365, resolve_base}, {0x70442020, resolve_base}, {0x61732020, resolve_as},
        {0x616e2020, resolve_an},   {0x61712020, resolve_aq},   {0x656c2020, resolve_el},   {0x65662020, resolve_ef},
    };
    return m;
}

// UGroup::ProcessBreadthFirst (0x002d6070) com um processador: os dados do grupo, depois cada subgrupo.
template <class F>
void process_breadth_first(UGroup* g, F&& on_data) {
    UData* items = UGroup_GroupEnd(g);
    for (uint32_t i = 0; i < g->count; ++i) on_data(g, items + i);
    UData* subs = UGroup_GetArray(g);
    for (uint32_t i = 0; i < UGroup_GroupCount(g); ++i) process_breadth_first(subs + i, on_data);
}

}  // namespace

uint32_t CARP_ResolveSymbolicReferences(UGroup* root, CarpSymbols& symbols) {
    UGroup* shared = UGroup_GroupLocateTag(root, 0x53686172);  // 'Shar'
    if (shared == UGroup_GroupEnd(root)) shared = root;
    // A passada simbólica (CARP::SymbolicResolver::ProcessData 0x002dc1b0).
    process_breadth_first(root, [&](UGroup*, UData* d) {
        if (UData_MatchTag(d) != 0x73722020) return;  // 'sr  '
        uint32_t size = 0;
        const uint32_t v = symbols.lookup(reinterpret_cast<const char*>(UData_Data(d)), size);
        ++g_stats.symbolic;
        if (!v) ++g_stats.symbolic_null;
        UData_AdoptData(d, v, size, 0, false);
    });
    // A passada das etiquetas (CARP::ResolveData 0x002da798).
    process_breadth_first(root, [&](UGroup* g, UData* d) {
        auto it = resolvers().find(UData_MatchTag(d));
        if (it == resolvers().end() || !it->second) return;
        ++g_stats.tag_records;
        it->second(d, shared, g);
    });
    UData* sect = UGroup_DataLocateTag(root, 0x53656374);  // 'Sect'
    if (sect == UGroup_DataEnd(root)) return 0;
    return *reinterpret_cast<const uint32_t*>(UData_Data(sect) + 0x7c);
}

const CarpResolveStats& carp_resolve_stats() { return g_stats; }

}  // namespace drv
