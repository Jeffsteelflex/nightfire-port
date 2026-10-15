#pragma once
// O driver de som das missões de carro no IOP: o SNDDRV.IRX da EA (o SND 7.42.0, "ps2/sndiop.c" nos STABS), portado do
// MIPS do módulo (os nomes da .symtab; a desmontagem em out/agent_drv_sound/ do checkout principal). O módulo não roda no
// interpretador: a parte 13 do nf_driving_check confere o que ele recebe do EE original (o RPC de iniciação, as
// transferências das amostras para a RAM da SPU2 e do IOP, byte a byte) e os testes (tests/test_driving_sound.cpp) o que
// se ouve (um banco feito à mão pela cadeia inteira: a frequência, o pitch, o fim no laço silencioso, a área do reverb).
// docs/driving.md seção 17. Os marcadores são `// [snddrv] 0x<endereço no .text do IRX> <Nome>`.
//
// O que ele faz, a cada tick (acordado quando a SPU2 acaba metade do buffer de saída: 512 amostras de 48 kHz):
//  - copia a "tagbuf" que o EE mandou (driving/sound/snd_state.h) e a percorre (SNDIOP_processtagbuf): o rótulo 3 traz
//    o estado de cada uma das 48 vozes da SPU2 (endereço da amostra com a geração, pitch, volume esquerdo e direito; os
//    bits de seco e de molhado; o reverb e o seu volume) e o IOP compara com o tick anterior e só manda o que mudou
//    (sceSdSetParam/SetAddr/SetSwitch: SSA, LSAX no laço silencioso de 0x5000, ADSR 0x000f/0x0005, key on/off; o volume
//    muda pela rampa da SPU2 quando o salto é grande, com a taxa da tabela volramplut); o rótulo 1 traz as amostras que
//    o mixador do EE fez, que o IOP soma às do seu próprio mixador e escreve na metade livre do buffer de saída;
//  - devolve ao EE o "ioptoee": o endereço atual (NAX) de cada voz (0 quando ela caiu no laço silencioso: acabou).
// As amostras dos bancos chegam pela fila de transferências (SNDIOP_dmqueue: sceSdVoiceTrans para a RAM da SPU2).
//
// A SPU2 fica atrás de SndLibsd (as funções do LIBSD.IRX que o driver chama): o nf_driving liga a do
// driving/sound/snd_spu.h (a platform/spu2.h), a conferência liga uma que registra as chamadas.
//
// Divergências (port):
//  - o PS2 roda o driver em duas threads do IOP (a do DMA, acordada pela interrupção da transferência em laço, e a
//    principal, acordada por ela) e recebe a tagbuf por DMA do SIF; aqui snddrv_iop_tick faz o trabalho das duas na
//    ordem delas, chamado pelo tick do EE (driving/sound/snd_ps2.cpp);
//  - as transferências para a RAM da SPU2 completam dentro da chamada (o PS2 espera a interrupção);
//  - só o caminho sem DTS (a saída estéreo do jogo: o modo 2) está portado; o DTS conta em snddrv_guards().

#include <cstdint>

namespace drv {

// O efeito de reverb (sceSdEffectAttr como o driver monta: o núcleo, o modo, as profundidades, o atraso, a
// realimentação).
struct SndEffectAttr {
    int32_t core = 0;
    int32_t mode = 0;
    int16_t depth_l = 0;
    int16_t depth_r = 0;
    int32_t delay = 0;
    int32_t feedback = 0;
};

// As funções do LIBSD.IRX que o SNDDRV.IRX chama (os números de parâmetro, chave e endereço do libsd: SD_VP_*,
// SD_P_*, SD_S_*, SD_A_*, com o núcleo no bit 0 e a voz x 2 nos bits 1..5).
class SndLibsd {
public:
    virtual ~SndLibsd() = default;
    virtual void Init(int32_t flag) = 0;
    virtual void SetParam(uint16_t entry, uint16_t value) = 0;
    virtual uint16_t GetParam(uint16_t entry) = 0;
    virtual void SetSwitch(uint16_t entry, uint32_t value) = 0;
    virtual void SetAddr(uint16_t entry, uint32_t addr) = 0;
    virtual uint32_t GetAddr(uint16_t entry) = 0;
    virtual void SetCoreAttr(uint16_t entry, uint16_t value) = 0;
    virtual void SetEffectAttr(int32_t core, const SndEffectAttr& attr) = 0;
    // Uma transferência do IOP para a RAM da SPU2 (mode 0: escrever); src é um bloco da memória do IOP.
    virtual void VoiceTrans(int32_t channel, int32_t mode, const uint8_t* src, uint32_t spu_addr, uint32_t size) = 0;
    // A transferência em laço da saída (BlockTrans no núcleo 0): o buffer, o tamanho; nulo para parar.
    virtual void BlockTrans(int32_t channel, int32_t mode, int16_t* buf, uint32_t size) = 0;
};

// [snddrv] 0x00009a3c start: zera o estado (o PS2 cria as threads e registra o RPC).
void snddrv_iop_start(SndLibsd* sd);
// [snddrv] 0x00009440 SNDIOP_dispatchrpc: fn 0 init (o bloco de 0x14 bytes do SNDPLATFORM_init), 1 restore, 2 o reverb
// do mixador.
void snddrv_iop_rpc(int32_t fn, const uint8_t* data, int32_t size);
// [snddrv] 0x000093f0 SNDIOP_cmdhandler (o comando 1: uma transferência de 'size' bytes do bloco do IOP para a SPU2);
// ee_id volta ao EE quando acaba (snd_ps2.cpp: SNDDRVI_dmcompleterequest).
void snddrv_iop_cmd(const uint8_t* iop_src, uint32_t dst, uint16_t size, uint16_t priority, int32_t ee_id);
// [snddrv] 0x000097dc threadmain (uma volta): copia e processa a tagbuf do EE e devolve o ioptoee (0x110 bytes).
void snddrv_iop_tick(const uint8_t* tagbuf, uint32_t* ioptoee);
// O IOP terminou a transferência ee_id (o comando 1 de volta ao EE): quem liga é snd_ps2.cpp.
using SndIopDmaDone = void (*)(int32_t ee_id);
void snddrv_set_dma_done(SndIopDmaDone fn);

uint64_t snddrv_guards();

// O alocador do kernel do IOP por trás do sceSifAllocIopHeap/sceSifFreeIopHeap do EE (AllocSysMemory: o primeiro vão
// que cabe, em unidades de 256 bytes). Os endereços são os de um modelo (a região de kIopHeapBase a kIopHeapEnd), não
// os do PS2 (que dependem dos módulos carregados antes); só o port e a conferência os usam, cada um com a sua instância.
class IopHeapModel {
public:
    static constexpr uint32_t kIopHeapBase = 0x00100000, kIopHeapEnd = 0x001f0000;
    // O endereço, ou 0 sem espaço.
    uint32_t alloc(uint32_t size);
    // 0, ou -1 se addr não é um bloco.
    int32_t free(uint32_t addr);
    void reset() { count_ = 0; }

private:
    struct Block {
        uint32_t addr, size;
    };
    Block blocks_[256] = {};
    int count_ = 0;
};

// A memória do IOP no port (2 MB): o EE escreve nela pelo DMA do SIF (o bloco das transferências para a SPU2, a parte
// do IOP dos bancos: as amostras EA-XA do mixador do IOP) e a aloca pelo modelo acima.
uint32_t snddrv_iop_alloc(uint32_t size);
int32_t snddrv_iop_free(uint32_t addr);
// [addr, addr + size) na memória do IOP; nulo fora dela.
uint8_t* snddrv_iop_ram(uint32_t addr, uint32_t size);

// O que o bloco de iniciação deixou no IOP (para a conferência e os relatórios).
struct SndIopInfo {
    uint8_t running = 0;
    int8_t outputs = 0;
    int8_t extra = 0;
    uint8_t iop_voices = 0;
    uint8_t dts = 0;
    uint16_t ee_rate = 0, iop_rate = 0, ee_frames = 0, iop_frames = 0;
    uint32_t updates = 0;
    uint32_t rpc_count[3] = {};      // as chamadas de RPC por função (0 init, 1 restore, 2 o reverb do mixador)
    uint32_t rpc_hash = 0x811c9dc5u;  // FNV-1a dos bytes de todas
};
SndIopInfo snddrv_iop_info();

}  // namespace drv
