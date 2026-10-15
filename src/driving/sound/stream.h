#pragma once
// O STREAM do PC (só do port): o lugar do librealfilez stream.o do DRIVING.ELF (STREAM_*: o buffer em anel dos fluxos do
// disco, enchido por uma thread que lê o DVD pelo FILESYS) e dos FILESYS_* que os fluxos usam (os arquivos BIG
// registrados pelo AStreamPriv: "driving/MIS01.mus", "driving/MIS01EN.spe"; os nomes "|all.asf" procurados neles).
// docs/driving.md seção 27.
//
// O SND (driving/sound/snd_stream.h) pede um arquivo .asf (STREAM_queuefile, até o pedaço 'SCEl'), pega os pedaços
// SCHl/SCDl/SCEl um a um (STREAM_get) e os devolve quando o tocador os acabou (STREAM_release). Aqui o arquivo inteiro
// é lido do disco do jogador (o BIGF pela platform/disc_source.h) na hora do pedido, e os pedaços "entram no buffer"
// na ordem enquanto cabem na capacidade que o original teria (STREAM_buffersize, a mesma conta), como um DVD
// instantâneo: STREAM_gettable é o que já entrou e ninguém pegou, STREAM_state 2 quando o próximo pedaço não cabe e 0
// quando tudo entrou. O PS2 lê o DVD com atrasos que o PC não reproduz; o resto (o que entra em cada pacote, a ordem, o
// fim de cada pedido) é o mesmo.
//
// Divergências: os pedaços ficam no arquivo copiado e não num anel (o espaço contado é a soma dos tamanhos, sem os
// alinhamentos e as marcas de volta do anel); um arquivo que não existe é recusado no STREAM_queuefile (o original
// aceita o pedido e o leitor falha depois: o AStream vê o erro na hora em vez de alguns quadros depois); a ordem da
// procura nos BIG registrados é a do registro (o "silence.asf" existe nos dois arquivos de Paris; o .mus vem antes).
//
// O StreamModel é o mesmo para o porte e para o lado emulado da conferência (a parte 23 do nf_driving_check roda o SND
// original no interpretador sobre outra instância dele): os endereços que devolve vêm do Placer (o porte: o endereço
// nativo dos bytes; a conferência: onde ela os copiou na RAM emulada).

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace drv {

class Bigf;

class StreamModel {
public:
    // Lê o arquivo "|nome.asf" (os BIG registrados); false se não existe.
    using Loader = std::function<bool(const std::string& name, std::vector<uint8_t>& out)>;
    // Dá o endereço (32 bits) do primeiro byte do arquivo lido.
    using Placer = std::function<uint32_t(std::vector<uint8_t>& data)>;

    StreamModel(Loader load, Placer place) : load_(std::move(load)), place_(std::move(place)) {}

    // [driving] 0x00305910 STREAM_overhead (a mesma conta)
    static int32_t overhead(int32_t requests, int32_t taps, int32_t filters) {
        return requests * 0x124 + taps * 0xc + filters * 0x10 + 0x1c0;
    }
    // O STREAM_create: devolve o identificador que o original devolveria (o endereço da tabela dos "taps" dentro da
    // memória) ou 0 (a memória não dá 0x1800 bytes de buffer, ou os números fora dos limites).
    uint32_t create(int32_t requests, int32_t taps, int32_t filters, uint32_t mem, int32_t size);
    void destroy();
    int32_t buffersize() const { return capacity_; }
    void setgreedylevel(int32_t level) { greedy_ = level; }
    uint32_t queuefile(const char* name, int32_t offset, uint32_t endtag);
    void kill();
    uint32_t get();                 // o endereço do pedaço, ou 0
    void release(uint32_t chunk);
    int32_t gettable() const { return avail_; }
    int32_t state() const { return state_; }
    uint32_t handle() const { return handle_; }
    // Para a conferência e os relatórios.
    int32_t usage() const { return usage_; }
    // O pedido e o deslocamento de um endereço dentro dos arquivos lidos (a conferência normaliza os ponteiros).
    bool locate(uint32_t addr, uint32_t* id, uint32_t* off) const {
        for (const auto& r : reqs_)
            if (addr >= r->addr && addr < r->addr + uint32_t(r->data.size())) {
                *id = r->id;
                *off = addr - r->addr;
                return true;
            }
        for (const auto& g : grave_)
            if (addr >= g.second->addr && addr < g.second->addr + uint32_t(g.second->data.size())) {
                *id = g.second->id;
                *off = addr - g.second->addr;
                return true;
            }
        return false;
    }
    // Algum pedido vivo usa o bloco que começa em addr.
    bool holds(uint32_t addr) const {
        for (const auto& r : reqs_)
            if (r->addr == addr) return true;
        for (const auto& g : grave_)
            if (g.second->addr == addr) return true;
        return false;
    }
    int32_t requests() const { return int32_t(reqs_.size()); }
    uint64_t files_read() const { return files_read_; }
    uint64_t missing() const { return missing_; }

private:
    struct Request {
        uint32_t id = 0;
        std::vector<uint8_t> data;
        uint32_t addr = 0;
        uint32_t endtag = 0;
        size_t next = 0;            // o próximo pedaço a entrar
        bool loaded = false;        // todos entraram
    };
    struct Chunk {
        Request* req = nullptr;
        uint32_t addr = 0;
        int32_t size = 0;
        bool got = false;
        bool released = false;
    };
    void fill();
    void trim();

    Loader load_;
    Placer place_;
    uint32_t handle_ = 0;
    int32_t capacity_ = 0;
    int32_t greedy_ = 0;
    int32_t max_requests_ = 0;
    int32_t usage_ = 0;
    int32_t avail_ = 0;
    int32_t state_ = 0;
    uint32_t next_id_ = 1;
    std::deque<std::unique_ptr<Request>> reqs_;
    // Os pedidos que já saíram ficam legíveis até entrar no buffer outra capacidade inteira de pedaços (no PS2 um pedaço
    // liberado continua no anel até o leitor escrever por cima: o canal de baixo de um par lê o seu pedaço depois que o
    // de cima o liberou).
    std::deque<std::pair<uint64_t, std::unique_ptr<Request>>> grave_;
    uint64_t loaded_total_ = 0;
    std::deque<Chunk> chunks_;
    uint64_t files_read_ = 0, missing_ = 0;
};

// O FILESYS do PC: os BIG registrados. O abridor dá um Bigf do disco do jogador por caminho ("driving/MIS01.mus").
using BigOpener = std::function<std::unique_ptr<Bigf>(const std::string& path)>;
void filesys_set_opener(BigOpener open);
// [driving] FILESYS_existssync, FILESYS_addbigsync, FILESYS_delbigsync (só o port: os BIG dos fluxos)
int32_t FILESYS_existssync(const char* path);
int32_t FILESYS_addbigsync(const char* path, int32_t* id);
void FILESYS_delbigsync(int32_t id);
// "|nome": o arquivo num BIG registrado (FILESYS_bypassqueuefileinfo); lê os bytes se out não é nulo.
bool filesys_find(const std::string& name, std::vector<uint8_t>* out);
void filesys_reset();

// A API do STREAM que o SND chama (o modelo do porte, por identificador).
int32_t STREAM_overhead(int32_t requests, int32_t taps, int32_t filters);
uint32_t STREAM_create(int32_t requests, int32_t taps, int32_t filters, uint8_t* mem, int32_t size);
void STREAM_destroy(uint32_t h);
int32_t STREAM_buffersize(uint32_t h);
void STREAM_setgreedylevel(uint32_t h, int32_t level);
uint32_t STREAM_queuefile(uint32_t h, const char* name, int32_t offset, uint32_t endtag);
void STREAM_kill(uint32_t h);
uint8_t* STREAM_get(uint32_t h);
void STREAM_release(uint32_t h, uint8_t* chunk);
int32_t STREAM_gettable(uint32_t h);
int32_t STREAM_state(uint32_t h);
StreamModel* stream_model(uint32_t h);
void stream_power_on();
// Só para os testes e a conferência: quem lê os arquivos dos modelos criados depois (vazio: o FILESYS).
void stream_set_loader(StreamModel::Loader load);

}  // namespace drv
