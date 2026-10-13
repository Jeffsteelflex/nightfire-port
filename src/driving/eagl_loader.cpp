// O EAGL::DynamicLoader (eagl_loader.h): o carregador dos objetos ELF do EAGL, portado do DRIVING.ELF.
// Divergências no cabeçalho de eagl_loader.h (mapas, mensagens, o handle interno).

#include "driving/eagl_loader.h"

#include <cstdio>
#include <cstring>
#include <map>

#include "platform/crt_compat.h"

namespace drv::eagl {

namespace {

struct TypeEntry {
    TypeCtor ctor;
    TypeDtor dtor;
};
struct AllocEntry {
    RuntimeAlloc alloc;
    RuntimeFree free;
};

std::map<std::string, TypeEntry>& types() {  // o mapa 0x348b00
    static std::map<std::string, TypeEntry> m;
    return m;
}
std::map<std::string, AllocEntry>& allocs() {  // o mapa 0x348b28
    static std::map<std::string, AllocEntry> m;
    return m;
}
std::map<std::string, uint32_t>& vars() {  // o mapa 0x348ae8 (RegisterVar)
    static std::map<std::string, uint32_t> m;
    return m;
}
LoaderHandle*& loaded_head() {  // DAT_00348b50: a lista dos objetos carregados
    static LoaderHandle* h = nullptr;
    return h;
}

void message(const char* fmt, const char* a = nullptr, int b = 0) {
    char buf[512];
    std::snprintf(buf, sizeof buf, fmt, a ? a : "", b);
    diagnostics().push_back(buf);
}

// O tipo depois do nome reescrito ("nome\0\x7fTipo"), ou a string vazia.
const char* type_after(const char* name) {
    const char* end = name + std::strlen(name);
    return end[1] == '\x7f' ? end + 2 : end;
}

}  // namespace

std::vector<std::string>& diagnostics() {
    static std::vector<std::string> d;
    return d;
}

uint32_t elf_hash(const char* s) {
    uint32_t h = 0;
    for (; *s; ++s) {
        h = (h << 4) + uint32_t(int8_t(*s));  // o char do GCC do EE é com sinal
        const uint32_t g = h & 0xF0000000u;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

void register_type(const std::string& type, TypeCtor ctor, TypeDtor dtor) { types()[type] = {std::move(ctor), std::move(dtor)}; }
void register_runtime_alloc(const std::string& type, RuntimeAlloc alloc, RuntimeFree free) {
    allocs()[type] = {std::move(alloc), std::move(free)};
}
void register_var(const std::string& name, uint32_t addr) { vars()[name] = addr; }
void unregister_var(const std::string& name) { vars().erase(name); }
bool registered_var(const std::string& name, uint32_t& addr) {
    auto it = vars().find(name);
    if (it == vars().end()) return false;
    addr = it->second;
    return true;
}
void reset_registries() {
    types().clear();
    allocs().clear();
    vars().clear();
    loaded_head() = nullptr;
}

DynamicLoader::DynamicLoader(uint8_t* elf, uint32_t size, Resolver resolver) : elf_(elf), datsize_(size) {
    Initialize(std::move(resolver));
    Resolve();
}

DynamicLoader::DynamicLoader(uint8_t* elf, uint32_t datsize, uint8_t* rel, bool defer_resolve, Resolver resolver)
    : elf_(elf), datsize_(datsize), rel_(rel) {
    Initialize(std::move(resolver));
    if (!defer_resolve) Resolve();
}

DynamicLoader::~DynamicLoader() {
    RunDestructors();
    for (const RuntimeObject& r : runtime_)
        if (r.free) r.free(r.addr, r.extra);
    Release();
}

// [driving] 0x0029ea00 EAGL::DynamicLoader::Initialize
void DynamicLoader::Initialize(Resolver resolver) {
    // A posição de um pedaço do arquivo como ponteiro: abaixo do tamanho da primeira parte, nela; acima, na segunda.
    auto map = [&](uint32_t off) -> uint8_t* {
        if (off < datsize_) return elf_ + off;
        if (rel_) return rel_ + (off - datsize_);
        if (datsize_ < off) return nullptr;
        return elf_ + off;
    };
    if (!elf_) return;
    const uint16_t shnum = *reinterpret_cast<uint16_t*>(elf_ + 0x30);
    const uint16_t shstrndx = *reinterpret_cast<uint16_t*>(elf_ + 0x32);
    ElfShdr* sections = reinterpret_cast<ElfShdr*>(map(*reinterpret_cast<uint32_t*>(elf_ + 0x20)));
    if (!sections) return;
    for (int i = 0; i < shnum; ++i) sections[i].offset = addr_of(map(sections[i].offset));
    const char* shstr = ptr_at<const char>(sections[shstrndx].offset);

    char* strtab = nullptr;
    ElfSym* symtab = nullptr;
    int32_t count = 0;
    for (int i = 0; i < shnum; ++i) {
        ElfShdr& s = sections[i];
        switch (s.type) {
        case 0:  // SHT_NULL
        case 1:  // SHT_PROGBITS
            break;
        case 2:  // SHT_SYMTAB
            s.link = sections[s.link].offset;
            if (std::strcmp(".symtab", shstr + s.name) == 0) {
                symtab = ptr_at<ElfSym>(s.offset);
                count = int32_t(s.size) / 16;
            }
            break;
        case 3:  // SHT_STRTAB: os nomes "__Tipo:::nome" viram "nome\0\x7fTipo"
            if (std::strcmp(".strtab", shstr + s.name) == 0) {
                strtab = ptr_at<char>(s.offset);
                char* p = strtab;
                int left = int(s.size);
                while (left > 0) {
                    const int len = int(std::strlen(p));
                    if (p[0] == '_' && p[1] == '_') {
                        char* sep = std::strstr(p + 2, ":::");
                        if (sep) {
                            char type[128];
                            *sep = 0;
                            nf::str_copy_trunc(type, sizeof type, p + 2);
                            const char* rest = sep + 3;
                            const int rlen = int(std::strlen(rest));
                            std::memmove(p, rest, size_t(rlen) + 1);
                            p[rlen + 1] = '\x7f';
                            std::strcpy(p + rlen + 2, type);
                        }
                    }
                    left = left - 1 - len;
                    p += len + 1;
                }
            }
            break;
        case 4:  // SHT_RELA
            if (s.size) message("dlopen: non empty SHT_RELA section seen. Not supported.\n");
            break;
        case 5:  // SHT_HASH
        case 6:  // SHT_DYNAMIC (o jogo dá a mesma mensagem)
            message("dlopen: SHT_HASH section seen. Not supported.\n");
            break;
        case 8:  // SHT_NOBITS
            if (s.size) message("dlopen: SHT_NOBITS section with data seen. Not supported.\n");
            break;
        case 9:  // SHT_REL: a tabela de símbolos e a seção alvo viram ponteiros
            s.info = sections[s.info].offset;
            s.link = sections[s.link].offset;
            break;
        case 10:
            message("dlopen: SHT_SHLIB section seen. Not supported.\n");
            break;
        case 11:
            message("dlopen: SHT_DYNSYM section seen. Not supported.\n");
            break;
        case 0x70000005:  // SHT_MIPS_*
        case 0x70000006:
        case 0x7ffff420:
        case 0x7ffff421:
            break;
        default:
            message("Unknown section type %s%d. Aborting dynamic loading.\n", "", int(s.type));
            break;
        }
    }
    if (!symtab) message("dlopen: Failed to find a symbol table. Aborting\n");

    auto* h = new LoaderHandle();
    h->strtab = strtab;
    h->count = count;
    h->symtab = symtab;
    h->sections = sections;
    h->elf = elf_;
    h->resolver = std::move(resolver);
    for (int32_t& b : h->buckets) b = -1;
    h->chain.assign(size_t(count), -1);
    for (int32_t i = 0; i < count && strtab; ++i) {
        const uint32_t hash = elf_hash(strtab + symtab[i].name) & 0xFF;
        h->chain[size_t(i)] = h->buckets[hash];
        h->buckets[hash] = i;
    }
    h->next = loaded_head();
    if (loaded_head()) loaded_head()->prev = h;
    h->prev = nullptr;
    loaded_head() = h;
    handle_ = h;
}

// [driving] 0x0029f8e8 EAGL::DynamicLoader::Release
void DynamicLoader::Release() {
    LoaderHandle* h = handle_;
    if (!h) return;
    if (!h->prev) loaded_head() = h->next;
    else h->prev->next = h->next;
    if (h->next) h->next->prev = h->prev;
    delete h;
    handle_ = nullptr;
}

// [driving] 0x002bcd28 EAGL::DynamicLoader::RunDestructors (endereço do SYM; o do ELF do disco não foi casado)
void DynamicLoader::RunDestructors() {
    for (const Destructor& d : destructors_)
        if (d.dtor) d.dtor(d.addr);
    destructors_.clear();
}

uint8_t* DynamicLoader::data() const {
    return handle_ && handle_->sections ? ptr_at(handle_->sections[1].offset) : nullptr;
}

uint32_t DynamicLoader::data_size() const { return handle_ && handle_->sections ? handle_->sections[1].size : 0; }

// [driving] 0x0029f5b0 EAGL::DynamicLoader::FindSymbol
uint32_t DynamicLoader::Find(const char* name) const {
    const LoaderHandle* h = handle_;
    if (!h || !h->strtab) return 0;
    const uint16_t shnum = *reinterpret_cast<uint16_t*>(h->elf + 0x30);
    for (int32_t j = h->buckets[elf_hash(name) & 0xFF]; j != -1; j = h->chain[size_t(j)]) {
        if (j > h->count) {
            message("dlsym: Internal error. Bad j value %s%d!\n", "", j);
            break;
        }
        const ElfSym& s = h->symtab[j];
        if (std::strcmp(name, h->strtab + s.name) == 0 && s.shndx != 0 && s.shndx < shnum)
            return h->sections[s.shndx].offset + s.value;
    }
    return 0;
}

namespace {

// O FindSymbol num outro objeto da lista (a mesma busca do Find, sem o objeto C++ em volta).
uint32_t find_in(const LoaderHandle* h, const char* name) {
    if (!h->strtab) return 0;
    const uint16_t shnum = *reinterpret_cast<uint16_t*>(h->elf + 0x30);
    for (int32_t j = h->buckets[elf_hash(name) & 0xFF]; j != -1; j = h->chain[size_t(j)]) {
        if (j > h->count) break;
        const ElfSym& s = h->symtab[j];
        if (std::strcmp(name, h->strtab + s.name) == 0 && s.shndx != 0 && s.shndx < shnum)
            return h->sections[s.shndx].offset + s.value;
    }
    return 0;
}

}  // namespace

// [driving] 0x0029e518 EAGL::DynamicLoader::Resolve
void DynamicLoader::Resolve() {
    LoaderHandle* h = handle_;
    if (!h || h->resolved) return;
    h->resolved = 1;
    const uint16_t shnum = *reinterpret_cast<uint16_t*>(h->elf + 0x30);
    ElfShdr* sections = h->sections;
    for (int si = 0; si < shnum; ++si) {
        const ElfShdr& rs = sections[si];
        if (rs.type != 9) continue;
        ElfSym* symtab = ptr_at<ElfSym>(rs.link);
        uint8_t* target = ptr_at(rs.info);
        const uint32_t* rel = ptr_at<uint32_t>(rs.offset);
        const int n = int(rs.size) / 8;
        for (int k = 0; k < n; ++k) {
            const uint32_t r_offset = rel[k * 2];
            const uint32_t r_info = rel[k * 2 + 1];
            ElfSym& sym = symtab[r_info >> 8];
            const char* name = h->strtab + sym.name;
            uint32_t S = 0;
            bool understood = false;
            while ((sym.info & 0xF) < 4) {
                if (sym.shndx != 0 && sym.shndx < shnum) {
                    S = sections[sym.shndx].offset + sym.value;
                    understood = true;
                    break;
                }
                sym.other = 2;  // importado
                uint32_t addr = 0;
                bool found = false;
                for (LoaderHandle* o = loaded_head(); o && !found; o = o->next) {
                    if (o == h) continue;
                    addr = find_in(o, name);
                    found = addr != 0;
                }
                if (!found && h->resolver) addr = h->resolver(name, found);
                if (!found) found = registered_var(name, addr);
                if (!found) {
                    static const char kRuntime[] = "RUNTIME_ALLOC::";
                    const char* type = type_after(name);
                    auto it = std::strncmp(kRuntime, name, sizeof kRuntime - 1) == 0 ? allocs().find(type)
                                                                                      : allocs().end();
                    if (it == allocs().end() || !it->second.alloc) {
                        message("ERROR: DynamicLoader::Resolve - Failed to resolve undefined relocation symbol %s.\n",
                                name);
                        S = sym.value;
                        understood = true;
                        break;
                    }
                    uint32_t extra = 0;
                    bool needs_free = false;
                    addr = it->second.alloc(name + sizeof kRuntime - 1, *this, extra, needs_free);
                    if (needs_free && addr) runtime_.push_back({it->second.free, addr, extra});
                }
                sym.shndx = 1;
                sym.value = addr - sections[1].offset;
            }
            if (!understood) {
                message("dlopen: Relocation to a symbol type I don't understand! (symbol %s type %d)\n", name,
                        sym.info & 0xF);
                S = 0;
            }
            uint32_t* p = reinterpret_cast<uint32_t*>(target + r_offset);
            switch (r_info & 0xFF) {
            case 0:
            case 1:
            case 3:
                break;
            case 2:  // R_MIPS_32
                *p += S;
                break;
            case 4: {  // R_MIPS_26
                const uint32_t t = S + (*p & 0x3FFFFFF) * 4;
                if (((addr_of(p) + 4) & 0xFC000000u) != (t & 0xFC000000u))
                    message("dlopen: Result of patching jmp instruction is outside of range of possible jump.\n");
                *p = (*p & 0xFC000000u) | (t & 0xFFFFFFF) >> 2;
                break;
            }
            case 5:  // R_MIPS_HI16, sem o vai-um do LO16 (como o jogo)
                *reinterpret_cast<uint16_t*>(p) = uint16_t(*reinterpret_cast<uint16_t*>(p) + (S >> 16));
                break;
            case 6:  // R_MIPS_LO16
                *reinterpret_cast<uint16_t*>(p) = uint16_t(*reinterpret_cast<uint16_t*>(p) + S);
                break;
            case 7:
            case 12:
                message("dlopen: Cannot deal with GP relative relocations. Aborting\n");
                break;
            case 8:
                message("dlopen: Cannot deal with MIPS_LITERAL relocation yet\n");
                break;
            case 9:
                message("dlopen: Cannot deal with MIPS_GOT16 relocation yet\n");
                break;
            default:
                message("dlopen: Cannot deal with relocation type %s%d yet\n", "", int(r_info & 0xFF));
                break;
            }
        }
    }
    RunConstructors();
}

// [driving] 0x0029f478 EAGL::DynamicLoader::GetSymbol
SymbolInfo DynamicLoader::GetSymbol(int index) const {
    SymbolInfo out;
    const LoaderHandle* h = handle_;
    if (!h || index < 0 || index >= h->count) return out;
    const ElfSym& s = h->symtab[index];
    out.name = h->strtab + s.name;
    out.type = type_after(out.name);
    out.defined = s.other != 2;
    const uint16_t shnum = *reinterpret_cast<uint16_t*>(h->elf + 0x30);
    if (s.other == 1) out.addr = s.value;
    else if (s.shndx != 0 && s.shndx < shnum) out.addr = h->sections[s.shndx].offset + s.value;
    return out;
}

// [driving] 0x0029f118 EAGL::DynamicLoader::GetAddr
bool DynamicLoader::GetAddr(const char* type, const char* name, uint32_t& addr) const {
    addr = 0;
    const LoaderHandle* h = handle_;
    if (!h || !h->strtab) return false;
    const uint16_t shnum = *reinterpret_cast<uint16_t*>(h->elf + 0x30);
    for (int32_t j = h->buckets[elf_hash(name) & 0xFF]; j != -1; j = h->chain[size_t(j)]) {
        if (j > h->count) break;
        const ElfSym& s = h->symtab[j];
        const char* sn = h->strtab + s.name;
        if (std::strcmp(name, sn) != 0 || std::strcmp(type, type_after(sn)) != 0) continue;
        if (s.other == 1) {
            addr = s.value;
            return true;
        }
        if (s.shndx != 0 && s.shndx < shnum) {
            addr = h->sections[s.shndx].offset + s.value;
            return true;
        }
    }
    return false;
}

// [driving] 0x0029e3b0 EAGL::DynamicLoader::RunConstructors
void DynamicLoader::RunConstructors() {
    const int n = GetCount();
    for (int i = 0; i < n; ++i) {
        const SymbolInfo s = GetSymbol(i);
        if (!s.defined) continue;
        auto it = types().find(s.type);
        if (it == types().end() || !it->second.ctor) continue;
        it->second.ctor(s.addr, *this);
        destructors_.push_back({it->second.dtor, s.addr});
    }
}

}  // namespace drv::eagl
