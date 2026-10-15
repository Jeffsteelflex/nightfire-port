#pragma once
// O CARP na memória como o jogo o usa (docs/driving.md seções 5 e 12): o UGroup do SupportPS2 depois de
// UGroup::Deserialize, a tabela de símbolos que o RCARPFile monta (os espaços de nomes "CARP", "EAGL", "CHAR") e as
// duas passadas de CARP::ResolveSymbolicReferences (as referências simbólicas 'sr', depois os 20 resolvedores por
// etiqueta). carp.h lê o arquivo sem mexer nele (o carro do protótipo); isto aqui é o caminho do jogo, que reescreve os
// cabeçalhos e as referências no lugar.
//
// O cabeçalho de um grupo ou de um dado (UData, 16 bytes) é {etiqueta, bandeiras, contagem, posição}; no arquivo a
// posição é relativa ao cabeçalho (bit 1 das bandeiras: o vetor do grupo em +posição x 16, os dados em +posição) e
// UGroup::ResolveOffsets a troca pelo endereço absoluto, apagando o bit 1. Aqui o endereço é a palavra de 32 bits do
// port (nf::p32_u: o próprio ponteiro em 32 bits, o deslocamento na janela da arena em 64), então o bloco do arquivo tem
// de morar na arena (std::vector serve: o operator new do port é o da arena em 64 bits).
//
// Os registros do CARP que o porte lê (os leiautes do PS2: o 'Base' tem 96 bytes no disco americano, o do Xbox 100;
// os campos que o código lê estão nos mesmos lugares, conferidos nos resolvedores do PS2 e no WRender::WRender):
//  - BaseDesc ('Base', 'pD'): o modelo, os atributos (PHYS::), a caixa, o nome;
//  - CarpInstance ('in', 'ci', 'Cams', 64 bytes): a matriz (linhas 0-2 a rotação, 3 a posição) com bandeiras e a
//    referência do artigo nos w;
//  - CarpArticle ('as', o WorldArticle do nightfire-cxbx): quatro referências (o modelo, os efeitos, as animações), o
//    número de LODs, a passada do WRender, o tipo de render, e os pares {distância, modelo do EAGL}.
//
// As referências ficam como estão no arquivo até a resolução: depois de CarpFile::resolve um TagReference é o endereço
// (p32) do que nomeia, ou 0.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "platform/ptr32.h"

namespace drv {

// O cabeçalho de um grupo ou de um dado (UGroup e UData do SupportPS2; os dois têm 16 bytes).
struct UData {
    uint32_t tag;
    uint32_t flags;   // bit 0: etiqueta curta (duas letras e um índice); bit 1: posição relativa; bits 5+: subgrupos
                      // (num grupo) ou bits 8+: o tamanho (num dado); 8/0x10: dados/subgrupos ordenados
    uint32_t count;   // os dados de um grupo; num dado, a contagem de elementos
    uint32_t offset;  // depois de Deserialize: o endereço (p32) do vetor do grupo ou dos dados
};
static_assert(sizeof(UData) == 16);
using UGroup = UData;

// A etiqueta para comparar ("as  " numa família curta): UData::MatchTag.
inline uint32_t UData_MatchTag(const UData* d) { return (d->flags & 1) ? ((d->tag & 0xFFFF0000u) | 0x2020u) : d->tag; }
inline uint32_t UData_Size(const UData* d) { return d->flags >> 8; }
// [driving] 0x002d6940 UGroup::GetArray
UData* UGroup_GetArray(const UGroup* g);
inline uint32_t UGroup_GroupCount(const UGroup* g) { return g->flags >> 5; }
inline UData* UGroup_GroupEnd(const UGroup* g) { return UGroup_GetArray(g) + UGroup_GroupCount(g); }
inline UData* UGroup_DataEnd(const UGroup* g) { return UGroup_GroupEnd(g) + g->count; }
// Os dados de um registro (a posição relativa antes de Deserialize, o endereço depois).
uint8_t* UData_Data(const UData* d);
// [driving] 0x002d51f0 UGroup::GroupLocateTag: o subgrupo com a etiqueta, ou UGroup_GroupEnd.
UGroup* UGroup_GroupLocateTag(const UGroup* g, uint32_t tag);
// [driving] 0x002d5a10 UGroup::DataLocateTag: o dado com a etiqueta (inteira: 0x61730001 = as#0001), ou UGroup_DataEnd.
UData* UGroup_DataLocateTag(const UGroup* g, uint32_t tag);
// [driving] 0x002d5920 UGroup::DataLocateFirst(tag, índice, máximo): índice -1 procura a etiqueta inteira; senão a
// família curta tag | índice, aceita se a etiqueta casar e o índice achado for <= máximo.
UData* UGroup_DataLocateFirst(const UGroup* g, uint32_t tag, int32_t index, int32_t max);
// [driving] 0x002d61b0 UGroup::ResolveOffsets (com 0x002d4930 UData::ResolveOffsets), pelo UGroup::Deserialize
// (0x002d4ce0): as posições relativas viram endereços, recursivamente.
void UGroup_Deserialize(UGroup* root);
// [driving] 0x002d45d8 UData::AdoptData(void *, int tamanho, int contagem, bool próprio)
void UData_AdoptData(UData* d, uint32_t data, uint32_t size, uint32_t count, bool own);
// A etiqueta de um texto "{as  0001}" (UDataGroupDecodeTag): quatro letras, e com o '}' na posição 9 o índice
// hexadecimal de 4 dígitos nos 16 bits de baixo.
uint32_t UDataGroupDecodeTag(const char* text);

// ---- os registros ----

// O artigo de um objeto ou do mundo ('as', o WorldArticle do nightfire-cxbx): 0x20 bytes e os pares de LOD.
struct CarpArticle {
    uint32_t refs[4];       // +0x00 o grupo do modelo, os efeitos, as animações, ? (TagReference)
    uint8_t unknown10[0xa];
    uint8_t system_id;      // +0x1a
    uint8_t mirrored_system_id;
    uint8_t lod_count;      // +0x1c
    uint8_t draw_pass;      // +0x1d a passada do WRender (WRender::WRender 0x21eae8 lê +0x1d)
    uint8_t render_type;    // +0x1e 0 mundo, 1 carro, 2 objeto
    uint8_t render_flags;   // +0x1f 1: sem névoa
    struct Lod {
        float distance;     // até onde este modelo vale (o último vale além)
        uint32_t model;     // TagReference: o __Model do EAGL
    } lods[1];
};
static_assert(sizeof(CarpArticle) == 0x28);

// Uma instância ('in', 'ci', 'Cams', 64 bytes; CARP::Instance).
struct CarpInstanceRec {
    float axis_x[3];
    uint8_t flags;          // +0x0c (WRender: 4 céu, 8 sem corte no longe, 0x40; WWorld: 1 objeto de cena, 0x10 animação)
    uint8_t proc_anim_type;
    uint16_t proc_anim_index;
    float axis_y[3];
    uint32_t article;       // +0x1c TagReference: o artigo ('as' resolvido)
    float axis_z[3];
    uint32_t extra;         // +0x2c (o EnviroDesc nos 16 bits de baixo)
    float pos[3];
    uint32_t packed_dims;   // +0x3c medidas empacotadas (CARP::Instance::SetDimensions)
};
static_assert(sizeof(CarpInstanceRec) == 64);

// A descrição de um artigo ('Base', 'pD': 96 bytes no PS2).
struct CarpBaseDesc {
    uint32_t model;         // +0x00 TagReference: o grupo do modelo
    uint32_t flags;
    uint32_t unknown08;
    uint32_t physics;       // +0x0c TagReference: os atributos (PHYS::)
    float bbox_min[4];      // +0x10
    float bbox_max[4];      // +0x20
    uint32_t unknown30;
    uint32_t name;          // +0x34 TagReference: 'Name' (o resolvedor grava 'Name' antes de resolver)
    uint32_t unknown38;
    uint32_t collision_prims;  // +0x3c
    float damage_min[4];    // +0x40
    float damage_max[4];    // +0x50
};
static_assert(sizeof(CarpBaseDesc) == 96);

// ---- a tabela de símbolos e a resolução ----

// A tabela de símbolos (USymbolTable) do ponto de vista do CARP: um nome "ESPAÇO::resto" vira um endereço (p32) e um
// tamanho. O porte tem os espaços "CARP" (os grupos do arquivo: UCarpNamespace), "CHAR" (o próprio texto) e os que quem
// carrega registra ("EAGL": os símbolos do modelo; "PHYS", "GIOT"... ainda não).
class CarpSymbols {
public:
    using Lookup = std::function<uint32_t(const char* name, uint32_t& size)>;
    // [driving] 0x002cd8c0 UCarpNamespace::AddCarpFile: o "<<Root>>", o "<<Map>>", o "<<Shared>>" e cada 'Arti' pelo
    // nome ('Name'); o primeiro nome registrado vale.
    void add_carp_file(UGroup* root);
    void add_namespace(const std::string& name, Lookup fn) { spaces_[name] = std::move(fn); }
    // USymbolTable::NameLookup: o espaço antes do primeiro "::"; 0 se não achou (e conta o espaço em `misses`).
    uint32_t lookup(const char* name, uint32_t& size);
    // [driving] 0x002cda80 UCarpNamespace::NameLookup: "Grupo::{etiqueta}" ou "Grupo::{etiqueta}::deslocamento".
    uint32_t carp_lookup(const char* name, uint32_t& size) const;
    std::map<std::string, int> misses;  // por espaço de nomes, os nomes que ficaram nulos
    int resolved = 0;

private:
    std::map<std::string, UGroup*> groups_;
    std::map<std::string, Lookup> spaces_;
};

// Port: os espaços de nomes que valem para toda tabela de símbolos enquanto registrados (o Track::load registra o "PHYS",
// o PhysicsNamespace do PS2: as "PhysicsData" dos objetos de cena). Um espaço da própria tabela vem antes.
std::map<std::string, CarpSymbols::Lookup>& carp_global_namespaces();

// [driving] 0x002daa18 CARP::ResolveSymbolicReferences: InitResolvers, a passada simbólica (0x002dc1b0
// CARP::SymbolicResolver::ProcessData: cada 'sr' tem o texto trocado pelo endereço, UData::AdoptData) e a das etiquetas
// (CARP::ResolveData 0x002da798 pelo mapa de 0x002da838), em largura (UGroup::ProcessBreadthFirst 0x002d6070); devolve a
// palavra +0x7c do 'Sect', 0 sem ele. O grupo 'Shar' (ou a raiz, sem ele) é o compartilhado dos TagReference.
uint32_t CARP_ResolveSymbolicReferences(UGroup* root, CarpSymbols& symbols);
// [driving] 0x002dab48 CARP::TagReference::TagReference(UGroup *shared, UGroup *local): 0 fica 0, -1 vira o grupo local,
// uma etiqueta vira os dados do registro dela no grupo local, ou no compartilhado, ou 0.
void CARP_TagReference(uint32_t* ref, const UGroup* shared, const UGroup* local);

// Os contadores da resolução (só do port): as listas 'el' e os eventos delas cuja função de RegisterEvent::ResolveEvent
// não foi portada (os dados ficam como no arquivo).
struct CarpResolveStats {
    int symbolic = 0, symbolic_null = 0, tag_records = 0, event_lists = 0, events_unresolved = 0;
};
const CarpResolveStats& carp_resolve_stats();

}  // namespace drv
