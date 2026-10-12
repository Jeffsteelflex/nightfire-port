#pragma once
// O carregador de objetos ELF do EAGL 4.06.01 (EAGL::DynamicLoader), portado do DRIVING.ELF (docs/driving.md seção 10).
//
// Os modelos, os render methods (eaglrm.o, bondrm.o), os esqueletos e os bancos de animação são objetos ELF32 MIPS
// relocáveis (e_type 1) com uma seção .data, as relocações R_MIPS_32 dela, e símbolos com o tipo no nome:
// "__<Tipo>:::<nome>" (`__Model:::vanquish::{as  0000}::Model[0].00`, `__EAGL::TAR:::{T0@41ed1954}TEX0::rrvw`,
// `__COORD4:::GAME::CarLightingDullProps`). O DynamicLoader:
//  1. Initialize (0x0029ea00): troca a posição de cada seção por um ponteiro (o ELF pode vir partido: as posições
//     abaixo do tamanho da primeira parte ficam nela, as outras na segunda, o .rel ou o 'ELFr' do CARP); reescreve os
//     nomes "__Tipo:::nome" como "nome\0\x7fTipo" (o símbolo passa a se chamar só "nome" e o tipo fica depois do zero);
//     monta o hash ELF dos nomes (256 baldes) e põe o objeto na lista global dos carregados;
//  2. Resolve (0x0029e518): aplica as relocações; um símbolo indefinido é procurado, nesta ordem, nos outros objetos
//     carregados, na função de resolução do dono (o jogo passa ResolveEAGLReferences: a tabela de símbolos do jogo,
//     GAME::*, STATE::*, as texturas), nas variáveis registradas (RegisterVar: as matrizes EAGL::ViewPort::gp*) e, com o
//     prefixo "RUNTIME_ALLOC::", no alocador do tipo; achado, vira um símbolo definido relativo à seção 1;
//  3. RunConstructors (0x0029e3b0, no fim do Resolve): chama o construtor registrado para o tipo de cada símbolo
//     definido no objeto ("Model" -> o construtor do modelo, "EAGL::TAR" -> o da textura) com o endereço dele.
//
// O port é de 32 bits, como o PS2: os ponteiros que as relocações escrevem na .data são endereços deste processo e
// cabem nos 4 bytes do campo, então as estruturas do EAGL ficam no mesmo leiaute do jogo.
// Divergências (só do port): os mapas de nomes do EAGL (as classes de mapa do REAL) viram std::map; as mensagens de
// erro do `dlopen` vão para uma lista (`diagnostics`) em vez do printf do REAL; o "handle" interno do carregador (0x828
// bytes no PS2) é uma struct com os mesmos campos (os baldes do hash como int32 e não int64).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace drv::eagl {

struct ElfShdr {  // Elf32_Shdr (0x28 bytes); o Initialize reescreve offset/link/info como ponteiros
    uint32_t name, type, flags, addr, offset, size, link, info, addralign, entsize;
};
struct ElfSym {  // Elf32_Sym (0x10 bytes)
    uint32_t name, value, size;
    uint8_t info, other;
    uint16_t shndx;
};
static_assert(sizeof(ElfShdr) == 0x28 && sizeof(ElfSym) == 0x10);

class DynamicLoader;

// A função de resolução do dono do objeto (ResolveEAGLReferences no jogo): devolve o endereço e liga `found`.
using Resolver = std::function<uint32_t(const char* name, bool& found)>;
// Construtor e destrutor de um tipo (os registrados no EAGL::Init: "Model" 0x002856b0, "EAGL::TAR" 0x0027c6a0).
using TypeCtor = std::function<void(uint32_t addr, DynamicLoader& loader)>;
using TypeDtor = std::function<void(uint32_t addr)>;
// Alocador de tempo de execução de um tipo (RUNTIME_ALLOC::...): recebe o resto do nome, devolve o endereço; com
// `needs_free` o carregador guarda o par para liberar no Release.
using RuntimeAlloc = std::function<uint32_t(const char* spec, DynamicLoader& loader, uint32_t& extra, bool& needs_free)>;
using RuntimeFree = std::function<void(uint32_t addr, uint32_t extra)>;

// O estado de um objeto carregado (o bloco de 0x828 bytes do jogo).
struct LoaderHandle {
    LoaderHandle* next = nullptr;
    LoaderHandle* prev = nullptr;
    char* strtab = nullptr;          // a seção ".strtab" (os nomes dos símbolos)
    uint32_t resolved = 0;           // o Resolve já rodou
    int32_t count = 0;               // número de símbolos
    ElfSym* symtab = nullptr;        // a seção ".symtab"
    ElfShdr* sections = nullptr;
    uint8_t* elf = nullptr;          // o cabeçalho ELF
    int32_t buckets[256];            // o primeiro símbolo de cada balde do hash, -1 vazio
    std::vector<int32_t> chain;      // o próximo símbolo do mesmo balde
    Resolver resolver;
};

// Um símbolo como o GetSymbol (0x0029f478) o devolve.
struct SymbolInfo {
    const char* name = nullptr;
    const char* type = nullptr;  // "" quando o nome não tinha tipo
    uint32_t addr = 0;
    bool defined = false;        // definido neste objeto (não importado)
};

class DynamicLoader {
public:
    // O construtor do jogo para um ELF inteiro (0x0029f7c8): Initialize, Resolve (com os construtores).
    DynamicLoader(uint8_t* elf, uint32_t size, Resolver resolver);
    // Para um ELF partido (0x0029f820): `elf` tem o cabeçalho e a .data até `datsize`, `rel` o resto; com
    // `defer_resolve` o Resolve fica para depois (o RCARPFile chama Resolve ele mesmo).
    DynamicLoader(uint8_t* elf, uint32_t datsize, uint8_t* rel, bool defer_resolve, Resolver resolver);
    ~DynamicLoader();
    DynamicLoader(const DynamicLoader&) = delete;
    DynamicLoader& operator=(const DynamicLoader&) = delete;

    // EAGL::DynamicLoader::Resolve (0x0029e518).
    void Resolve();
    // EAGL::DynamicLoader::RunConstructors (0x0029e3b0).
    void RunConstructors();
    // EAGL::DynamicLoader::GetSymbol (0x0029f478).
    SymbolInfo GetSymbol(int index) const;
    int GetCount() const { return handle_ ? handle_->count : 0; }
    // EAGL::DynamicLoader::GetAddr (0x0029f118), por tipo e nome.
    bool GetAddr(const char* type, const char* name, uint32_t& addr) const;
    // O endereço de um símbolo definido pelo nome (o FindSymbol que o Resolve usa nos outros objetos).
    uint32_t Find(const char* name) const;

    LoaderHandle* handle() const { return handle_; }
    bool ok() const { return handle_ != nullptr; }
    // A .data (seção 1) e o tamanho dela.
    uint8_t* data() const;
    uint32_t data_size() const;

private:
    void Initialize(Resolver resolver);
    void Release();
    void RunDestructors();

    LoaderHandle* handle_ = nullptr;
    struct Destructor {
        TypeDtor dtor;
        uint32_t addr;
    };
    std::vector<Destructor> destructors_;
    struct RuntimeObject {
        RuntimeFree free;
        uint32_t addr, extra;
    };
    std::vector<RuntimeObject> runtime_;
    uint8_t* elf_ = nullptr;
    uint32_t datsize_ = 0;
    uint8_t* rel_ = nullptr;
};

// Os registros globais do EAGL (os mapas 0x348b00, 0x348b28 e 0x348ae8 do jogo).
void register_type(const std::string& type, TypeCtor ctor, TypeDtor dtor);
void register_runtime_alloc(const std::string& type, RuntimeAlloc alloc, RuntimeFree free);
// EAGL::DynamicLoader::RegisterVar e a busca no mapa dele (0x0029db60).
void register_var(const std::string& name, uint32_t addr);
void unregister_var(const std::string& name);
bool registered_var(const std::string& name, uint32_t& addr);
// Limpa os registros e a lista dos objetos carregados (testes, troca de modelo).
void reset_registries();

// O hash dos nomes (o hash do ELF, `elf_hash`, como o EAGL o calcula).
uint32_t elf_hash(const char* s);

// As mensagens do carregador (os printf do REAL): uma por erro, na ordem.
std::vector<std::string>& diagnostics();

// Endereço <-> ponteiro (o port é de 32 bits: os campos de ponteiro das estruturas do EAGL têm 4 bytes).
inline uint32_t addr_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
template <class T = uint8_t>
inline T* ptr_at(uint32_t a) { return reinterpret_cast<T*>(static_cast<uintptr_t>(a)); }
inline uint32_t rd32(uint32_t a) { return *ptr_at<uint32_t>(a); }

}  // namespace drv::eagl
