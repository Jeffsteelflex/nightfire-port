#pragma once
// Os modelos do EAGL (EAGL::Model) e as primitivas deles (EAGL::GeoPrim), portados do DRIVING.ELF (docs/driving.md
// seção 10).
//
// O modelo (o símbolo "__Model:::<nome>", ex. `vanquish::{as  0000}::Model[0].00` é o próprio objeto): +0x00 a
// lista dos nomes das texturas, +0x0c uma matriz 4x4, ..., +0xb4 os nomes das primitivas, +0xc4 a visibilidade de cada
// grupo (pares de u16; o segundo diz se o grupo é desenhado), +0xc8 o número de grupos, +0xcc a lista de desenho:
// cabeçalhos de grupo de 3 palavras {bandeiras | nível em 16 bits, ?, palavras do grupo} e entradas de primitiva de 2
// palavras {bandeiras | 0xffff, ponteiro para o "handle" da primitiva}; termina numa palavra de nível <= o primeiro.
// A primitiva (GeoPrim, 0x18 bytes): +0 a lista de comandos, +8 o render method, +0xc o máximo de vértices por lote,
// +0x14 o nome. A lista de comandos (palavras: operação no byte de cima, contagem nos 16 de baixo) diz o que mandar à VU1:
//   1 o microprograma (sobe se não estiver na VU, e o pacote de partida com BASE/OFFSET/MSCAL quando o método muda);
//   3 as texturas (TAR) que precisam estar na VRAM;  4 qwords de DMA prontos (a etiqueta CALL para os vértices);
//   5 dados por REF de cópia (as matrizes EAGL::ViewPort::gp*, as propriedades de luz do carro) depois de n palavras de
//     VIF (STMASK, STCYCL, STMOD e a UNPACK);  6 o pacote GS de um TAR (TEX0, CLAMP...);  7 um GeoPrimState (com FRAME,
//     ZBUF e FBA da hora);  0xe/0xf abrem e fecham uma etiqueta CNT;  0x10/0x11 saltam para outra lista (o construtor
//     do modelo encadeia assim as primitivas de um grupo);  0x12/0x13 reenviam um bloco por REF.
//
// Divergência (só do port): o salto 0x10/0x11 guarda o endereço da lista / 4 em 24 bits (até 128 MB), o que serve à RAM
// de 32 MB do PS2 mas não aos endereços de um processo de 32 bits do PC; o port guarda (alvo - base) / 4, com a base = o
// começo da .data do objeto que tem o modelo, passada ao desenho (`DrawContext::cmd_base`).

#include <cstdint>
#include <string>
#include <vector>

#include "driving/eagl_loader.h"

namespace drv::eagl {

// EAGL::Model::Model (0x00284c40) (o construtor de tipo de "Model", via 0x002856b0)
void Model_Construct(uint32_t model);
// Só do port: a base dos saltos 0x10/0x11 para os próximos Model_Construct (o construtor de tipo registrado põe o
// começo da .data do objeto; ver o cabeçalho).
void Model_SetCommandBase(uint32_t base);
// EAGL::GeoPrim::Construct (0x002a5740) (chamado pelo construtor do modelo, uma vez por primitiva da lista)
bool GeoPrim_Construct(uint32_t geoprim, uint32_t handle, int index_in_group);

// O que o desenho precisa do resto do motor: os registradores globais do GS e o quadro da animação.
struct DrawContext {
    uint32_t cmd_base = 0;     // base dos saltos 0x10/0x11 (só do port, ver o cabeçalho)
    int frame = 0;             // DAT_003483fc: o quadro das texturas e dos dados animados
    uint32_t instance_vars = 0;  // os parâmetros por instância (ops 0x15-0x19; 0 sem)
    uint64_t gs_frame = 0, gs_zbuf = 0, gs_fba = 0;  // DAT_00348398, DAT_003483a0, DAT_003483a8
};

// O buffer de DMA que o desenho enche (o DmaMgr do jogo), alinhado a 16 bytes.
class DmaBuffer {
public:
    explicit DmaBuffer(size_t capacity_qw = 1 << 16);
    uint8_t* begin() { return base_; }
    uint8_t* cur() { return cur_; }
    size_t used() const { return size_t(cur_ - base_); }
    void reset() { cur_ = base_; }
    void put32(uint32_t v);
    void put64(uint64_t v);
    void copy(const void* src, size_t bytes);
    void align16();
    void advance(size_t bytes) { cur_ += bytes; }
    bool overflow() const { return overflowed_ || cur_ > base_ + cap_; }
    // Só do port: cópias de um endereço nulo (uma referência que o carregador não resolveu) viram zeros e são contadas;
    // o que passaria do fim do buffer não é escrito (overflow()).
    int null_sources() const { return null_sources_; }
    void note_null_source() { ++null_sources_; }
    void clear_errors() {
        null_sources_ = 0;
        overflowed_ = false;
    }

private:
    bool room(size_t bytes);
    std::vector<uint8_t> store_;
    uint8_t* base_ = nullptr;
    uint8_t* cur_ = nullptr;
    size_t cap_ = 0;
    int null_sources_ = 0;
    bool overflowed_ = false;
};

// O render method (o símbolo "<Nome>" num eaglrm.o/bondrm.o): +0 o molde da lista de comandos, +0xc o máximo de
// vértices, +0x10 os parâmetros {nome, tamanho}, +0x14 o nome. O microprograma ("<Nome>0__EAGLMicroCode"): +0 número de
// instruções, +8 o endereço na VU (-1 fora), +0xc o pacote de partida (3 qwords: CNT, FLUSH/STCYCL/STMASK, STMOD/BASE/
// OFFSET/MSCAL), +0x10 os pedaços MPG.
struct DrawStats {
    int geoprims = 0, microcode_switches = 0, microcode_uploads = 0, unported_ops = 0;
    int last_unported_op = 0;
    // Só do port: os microprogramas na ordem dos pacotes de partida (um MSCAL cada), para quem executa a cadeia saber
    // qual método cada lote usa (o endereço na VU pode ser reaproveitado no meio de uma cadeia).
    std::vector<uint32_t> microcode_sequence;
};

// EAGL::GeoPrim::Draw (0x002a3290): põe a cadeia de DMA de uma primitiva no buffer (sem a etiqueta END).
// Devolve o render method (o ponteiro +8 da primitiva) para quem decodifica os lotes.
uint32_t GeoPrim_Draw(uint32_t geoprim, DmaBuffer& out, const DrawContext& ctx, DrawStats& stats);
// Esquece o microprograma corrente (DAT_00348c00) e os residentes: o próximo desenho manda tudo de novo.
void GeoPrim_ResetMicrocode();

// Os grupos de um modelo como o desenho (0x002848b0) os percorre: as primitivas (a primeira de cada grupo, as outras
// encadeadas pelo construtor) dos grupos visíveis.
struct ModelGroup {
    int index = 0;
    int level = 0;
    bool visible = true;
    std::vector<uint32_t> geoprims;
};
std::vector<ModelGroup> Model_Groups(uint32_t model);

// O nome de uma primitiva (+0x14) e do render method (+0x14 dele).
const char* GeoPrim_Name(uint32_t geoprim);
const char* RenderMethod_Name(uint32_t method);

}  // namespace drv::eagl
