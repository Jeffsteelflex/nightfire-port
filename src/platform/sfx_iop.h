#pragma once
// O driver de som do IOP (MODULES/SFX.IRX, o "EuroSound" da Eurocom: Q:/SS/source/SFX/SFX_Main.cpp) no PC.
//
// No PS2 o jogo (EE) junta comandos num lote (SFXBatchCommands, platform/psi_sound.h) e o manda ao IOP uma vez por
// VBlank por RPC (sceSifCallRpc); o SFX.IRX executa os comandos (sifrpc_server: começar sons, posições, ouvinte,
// volumes, pausa, bancos), toca as amostras na SPU2 e devolve SFXActiveData (os sons vivos). Aqui sfx_iop_rpc faz o
// papel do RPC: executa o lote na hora sobre a SPU2 do PC (platform/spu2.h).
//
// O código é o do SFX.IRX (o módulo tem .symtab e STABS; ver docs/formats.md, "Áudio"), com os mesmos nomes e
// estruturas; cada função leva "// [irx] 0x<endereço no .text do IRX> <Nome>" (o tools/decomp_status.py só conta
// o ACTION.ELF). As decisões do driver são preservadas: itens (SFXItem, 40), as alças de voz (100), as 48 vozes da
// SPU com a voz de trás dos sons 3D, o limite de vozes por SFXStartedCount, as prioridades e o MaxVoices, o volume
// (ES_CombineVolumes, ao quadrado), o 3D inteiro em decímetros com a raiz de Newton do driver, o pan 2D, o ambiente
// (reverb e salas abertas), fade, ducker, pausa (pitch 0) e a pausa das cenas, o PRNG, e o bug do key on atrasado
// (PS2_SampleSdSetSwitch zera os bits abaixo da voz com "& -bit").
//
// Divergências (comentadas nas funções):
//  - Arquivos: o driver lê do CD (sceCdRead, threads); aqui são lidos na hora do disco (a pasta PS2/ dos dados).
//  - Streams (falas): no PS2 os dados vêm do CD em pedaços para dois buffers da RAM de som (SFXUpdateStreams,
//    marcadores, ES_UpdateStreamMemCpy); aqui a fala inteira é lida para a memória ao começar e a voz a toca de lá
//    (Spu2::set_stream). O fim da fala é o fim do ADPCM (o PS2 para no marcador de fim, 0x30 bytes antes). A fala
//    começa no mesmo lote (no PS2 alguns VBlanks depois, o tempo de buscar no CD).
//  - Música (Streams[0], MFX_n): lida do disco aos poucos pela linha do tempo dos marcadores (MusicStream); um salto
//    que espera o próximo marcador o acha depois do que já tocou (o PS2 lê até ~2 s adiante); um trecho além da
//    tabela da faixa é ignorado.
//  - psiRequestVoiceHandle sem alça livre escreveria fora de VoiceHandleData: devolve -1.
//  - StartSample com uma amostra fora do banco do item (a fila de um item recusado, nunca preenchida: nula, ou de uma
//    vida anterior do item) não começa nada e conta em g_sfx_iop_stats.guards; o IRX a leria (o endereço 0 do IOP).
//  - DelayThread (as esperas entre registradores) não existe: a voz de trás liga 24 amostras (500 us) depois.

#include <cstdint>
#include <filesystem>

#include "platform/spu2.h"

namespace nf {

// A SPU2 que o driver programa (o áudio do jogo sai dela: platform/audio_out.h).
Spu2& spu();

// A raiz dos dados (a pasta com PS2/; padrão: nada carrega). Chamado por main.cpp e pelos verificadores.
void sfx_iop_set_root(const std::filesystem::path& root);
const std::filesystem::path& sfx_iop_root();

// O RPC: executa os comandos (até o 5 ou o 0x10) e copia os 0x154 bytes de SFXActiveData para out (pode ser nulo).
// Antes, a SPU avança o tempo passado desde o último lote (frames de 48 kHz contados por audio_out_tick).
void sfx_iop_rpc(const int32_t* commands, int32_t count, uint8_t* active_out);

// Volta o driver ao estado do boot (testes): itens, bancos, streams, a SPU.
void sfx_iop_debug_power_on();
// Depuração (só do port): com NF_SFX_LOG=<arquivo>, uma linha no registro do som (o driver e Sound_*); sem, nada.
void sfx_debug_log(const char* fmt, ...);

// A raiz quadrada do driver (PS2_SquareRoot), para os testes da distância.
uint32_t sfx_iop_square_root(uint32_t n);

// Contadores do port para o relatório e o nf_loader_check (desde o boot ou o último reset dos contadores).
struct SfxIopStats {
    uint32_t requested = 0;      // SFXStart/SFXStart3D recebidos
    uint32_t started = 0;        // itens criados com pelo menos uma voz ligada
    uint32_t rejected = 0;       // recusados (sem banco com o som, limite de vozes, prioridade, MaxVoices)
    uint32_t not_found = 0;      // o som não está nos bancos carregados
    uint32_t speech = 0;         // falas começadas (streams)
    uint32_t stolen = 0;         // itens mortos para dar lugar a outro
    uint32_t guards = 0;         // só no port: amostras fora do banco do item que StartSample não começou
    uint32_t banks_loaded = 0;
    uint32_t last_bank = 0xffffffff;
};
extern SfxIopStats g_sfx_iop_stats;
void sfx_iop_reset_stats();

// Estado para o relatório: itens vivos, vozes da SPU ligadas, falas tocando, o volume de efeito (reverb).
struct SfxIopState {
    int items = 0;
    int voices = 0;
    int speech_playing = 0;
    int bank_slot = -1;          // o banco carregado (SB_n), -1 nenhum
    int reverb = 0;              // SFXReverb (0-100)
    int paused = 0;              // SFXPaused
    int nis_playing = 0;
};
SfxIopState sfx_iop_state();

}  // namespace nf
