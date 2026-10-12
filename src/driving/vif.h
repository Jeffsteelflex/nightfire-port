#pragma once
// O caminho dos dados do EE para a VU1 no PS2: o DMA do canal 1 (VIF1) em modo cadeia e o VIF1 desempacotando na
// memória de dados da VU1 (docs/driving.md seção 10). Só do port: é o hardware, não código do jogo.
//
// O EAGL não manda vértices "prontos": cada GeoPrim monta uma cadeia de DMA (etiquetas CNT/REF/CALL/RET/NEXT/END, com
// as duas palavras de cima de cada etiqueta indo para o VIF, TTE = 1) cujos comandos do VIF (UNPACK, STMOD, STMASK,
// STROW, STCYCL, BASE, OFFSET, MSCAL, MSCNT...) põem os parâmetros e os vértices na memória da VU1 nos endereços que o
// microprograma do render method espera, com a memória dividida em dois buffers (BASE e BASE + OFFSET) que se alternam
// a cada MSCAL/MSCNT (o "TOPS" das UNPACK com a bandeira FLG). Os modelos dependem disso: a segunda primitiva de um
// grupo só sobe as constantes para o buffer que a primeira não usou, e as seguintes só mandam vértices. Para saber o que
// o microprograma vê, o port executa a cadeia como o hardware e entrega a memória da VU1 a cada MSCNT.
//
// Fidelidade: as UNPACK seguem o manual do EE (S/V2/V3/V4 de 32/16/8 bits e V4-5, USN, máscaras de 2 bits por campo,
// os modos de STMOD "offset" e "difference", o ciclo CL/WL com escrita pulada ou preenchida). Divergências: nas V2 o
// z/w recebem x/y e nas V3 o w recebe 0 (o hardware deixa o que a leitura seguinte trouxer; nenhum microprograma do
// carro lê esses campos); MPG não carrega microcódigo (ele não roda no PC); DIRECT/DIRECTHL vão para um callback.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace drv {

struct VuMemory {
    alignas(16) uint32_t w[1024 * 4];  // 16 KB = 1024 qwords
    float f(int qw, int c) const;
    uint32_t u(int qw, int c) const { return w[((qw & 1023) << 2) | c]; }
    int32_t i(int qw, int c) const { return int32_t(u(qw, c)); }
};

struct VifState {
    uint32_t row[4] = {0, 0, 0, 0};
    uint32_t col[4] = {0, 0, 0, 0};
    uint32_t mask = 0;
    int mode = 0;       // STMOD
    int cl = 1, wl = 1;  // STCYCL
    uint32_t base = 0, offset = 0, tops = 0, top = 0, itops = 0, itop = 0;
    bool dbf = false;
};

class Vif1 {
public:
    enum Kick { kMscal, kMscalf, kMscnt };
    // Chamado a cada MSCAL/MSCALF/MSCNT, antes da troca dos buffers: `top` é o buffer que o microprograma vai ler.
    std::function<void(Kick kind, uint32_t addr, uint32_t top, uint32_t itop)> on_kick;
    // Chamado com os dados de um DIRECT/DIRECTHL (o GIF no caminho 2).
    std::function<void(const uint32_t* qw, int count)> on_direct;

    VuMemory mem;
    VifState st;
    std::vector<std::string> errors;
    int unpacks = 0, kicks = 0;
    // As UNPACK desde o último MSCAL/MSCNT (só do port: os decodificadores substitutos acham os fluxos por elas).
    struct UnpackRecord {
        int cmd = 0;          // o comando (0x60-0x7f)
        uint32_t addr = 0;    // o endereço absoluto do primeiro qword escrito
        int num = 0;          // qwords escritos
    };
    std::vector<UnpackRecord> unpack_log;

    Vif1();
    void reset();
    // Processa palavras do VIF (comandos e dados).
    void process(const uint32_t* words, size_t count);
    // Executa uma cadeia de DMA a partir de `tag` (endereço de memória do processo, alinhado a 16), com TTE.
    // Para em END/REFE, num RET com a pilha vazia ou depois de `max_tags` etiquetas.
    void run_dma_chain(uint32_t tag, int max_tags = 1 << 20);

private:
    void unpack(uint32_t code, const uint32_t* data, size_t avail_words, size_t& used_words);
    // Restos de um comando que atravessa o fim de um pedaço de DMA (UNPACK, STROW... partidos entre etiquetas).
    std::vector<uint32_t> pending_;
};

}  // namespace drv
