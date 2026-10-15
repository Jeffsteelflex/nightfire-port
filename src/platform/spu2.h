#pragma once
// A SPU2 do PS2 no PC (só do port): o "hardware" que o driver de som do IOP (platform/sfx_iop.h) programa.
//
// 48 vozes (24 por núcleo; a voz v é a do núcleo v / 24) que tocam PS-ADPCM da RAM de som (2 MB) ou, no PC, de um
// buffer fora dela (as falas e a música em streaming: set_stream), com:
//   - pitch da SPU (0x1000 = 48 kHz; até 0x3fff), contador fracionário de 12 bits como no hardware;
//   - volume esquerdo/direito no modo fixo (registrador de 15 bits: 0..0x3fff positivo, 0x4000..0x7fff negativo, a
//     fase invertida que o driver usa na voz "de trás" dos sons 3D);
//   - o envelope que o driver programa em todas as vozes (ADSR1 0x000f, ADSR2 0x1fc0): ataque linear de 3 amostras
//     (+0x3800 por amostra), sustentação cheia, soltura linear de 2 amostras (-0x4000); ENVX = o nível do envelope;
//   - o laço e o fim pelas flags do ADPCM (bit 2 guarda o início do laço, bit 0 com o bit 1 volta a ele, sem o bit 1
//     zera o envelope e a voz para: a voz livre, ENVX 0);
//   - key on/off por voz, key on atrasado em amostras (o driver liga a voz de trás 500 us depois da da frente);
//   - os dois núcleos da SPU2 e o caminho do núcleo 0 ao 1, como o LIBSD.IRX os deixa (sceSdInit, 0x00000ee0 do
//     módulo: MMIX 0xff0 no núcleo 0 e 0xffc no 1, AVOL do núcleo 1 = 0x7fff, BVOL = 0, todas as vozes em VMIXL/R):
//       núcleo c: seco_c = sat16(soma das vozes de c), molhado_c = sat16(soma das vozes de c com VMIXEL/R)
//                 (as somas são amostras de 16 bits: a SPU2 as grava na "área de saída" da RAM, 0x1000-0x1fff);
//       núcleo 0: saída_0 = sat16((seco_0 + E + reverb_0(molhado_0 + E) x EVOL_0) x MVOL_0)  (gravada em 0x800/0xa00)
//       núcleo 1: saída = sat16((seco_1 + saída_0 x AVOL_1 + reverb_1(molhado_1) x EVOL_1) x MVOL_1)
//     A entrada externa do núcleo 1 (a saída do 0) vai só ao seco (MMIX 0xffc: bits 0-1 desligados). E é a entrada de
//     dados do núcleo 0 (a transferência em bloco dos filmes, platform/psi_movie.h) vezes o BVOL dele (changeInputVolume;
//     0 fora dos filmes), no seco e no reverb pelo MMIX 0xff0 (bits 4-7). No PC ela é uma fila de amostras estéreo de
//     48 kHz (input_push), consumida uma por amostra de saída enquanto ligada (input_start); vazia, entra silêncio (no PS2
//     a transferência em laço repetiria o buffer velho do IOP). Os volumes: MVOL e os das vozes no modo fixo de 15 bits (0x3fff = 0x7ffe =
//     ~1.0); EVOL, AVOL e BVOL são de 16 bits com sinal (0x7fff = ~1.0, 0x3fff = ~0.5), como os volumes de reverb e de
//     entrada da SPU do PS1 (vLOUT/vROUT, CD e externo) de que a SPU2 deriva.
// A saída passa por um bloqueio de corrente contínua (passa-altas de um polo em ~4 Hz), como a saída analógica do
// PS2, que é acoplada por capacitor: uma voz congelada pelo driver (pausa = pitch 0) segura um valor constante, que no
// PS2 vira um clique e silêncio, não um nível DC na caixa de som.
//
// O SNDDRV.IRX das missões de carro (driving/sound/snddrv_iop.h) usa mais do hardware, e só ele chama estas (o SFX.IRX
// não: o som a pé sai igual): a soltura do ADSR pela taxa do registrador (ADSR2 0x0005: -0x200 por amostra), o endereço
// de laço escrito pelo driver (LSAX, que fica no key on), o endereço atual (NAX), o volume atual (VOLX), a varredura de
// volume (o bit 15 do registrador: linear ou exponencial, subindo ou descendo no módulo, a fase negativa, a taxa nos 7
// bits de baixo, como o ADSR), as chaves do seco por voz (VMIXL/R), o MMIX de cada núcleo e os outros tipos de reverb
// do LIBSD.IRX (a tabela de presets em 0x41c8, 0x44 bytes cada).
//
// Divergências: a SPU2 interpola com uma tabela gaussiana de 4 pontos; aqui é Hermite de 4 pontos (a mesma
// janela, curva parecida). O reverb é o algoritmo da SPU com o preset "studio C" (o modo 4 que o driver escolhe),
// a 24 kHz (uma amostra a cada duas de 48 kHz, por média simples; o hardware filtra com um FIR), um por núcleo, como
// no hardware (cada núcleo tem a sua área de trabalho); os coeficientes vêm da tabela do LIBSD.IRX (spu2.cpp). Sem
// ruído, sem FM, sem "pitch modulation" (o driver não usa).

#include <cstdint>
#include <vector>

namespace nf {

// Estado do decodificador de PS-ADPCM (os dois últimos valores).
struct AdpcmState {
    int32_t s1 = 0;
    int32_t s2 = 0;
};

// Decodifica um bloco de 16 bytes em 28 amostras; devolve as flags (byte 1).
uint8_t adpcm_decode_block(const uint8_t* block, int16_t out[28], AdpcmState& st);

// Só no PC: uma fonte de blocos para uma voz (a música em streaming: o driver escolhe cada bloco pelos marcadores,
// como o PS2 escolhe o que copia para o buffer da voz). Devolve o próximo bloco de 16 bytes (as flags são usadas
// como as da RAM) ou nulo (sem dados: a voz para como num fim sem repetição).
struct Spu2BlockSource {
    virtual ~Spu2BlockSource() = default;
    virtual const uint8_t* next_block(int voice) = 0;
};

class Spu2 {
public:
    static constexpr int kVoices = 48;
    static constexpr uint32_t kRamBytes = 2u << 20;
    static constexpr int kRate = 48000;

    Spu2();
    void reset();  // vozes paradas, registradores zerados, RAM preservada

    std::vector<uint8_t> ram;  // a RAM de som (o driver copia os bancos para cá)

    // Registradores de voz (os parâmetros de sceSdSetParam/SetAddr que o driver usa).
    void set_volume_left(int v, uint16_t reg);   // SD_VP_VOLL
    void set_volume_right(int v, uint16_t reg);  // SD_VP_VOLR
    void set_pitch(int v, uint16_t pitch);       // SD_VP_PITCH
    void set_start(int v, uint32_t addr);        // SD_VA_SSA (bytes na RAM); desfaz set_stream
    // Só no PC: a voz lê de um buffer fora da RAM (as streams). O buffer tem de viver até a voz parar.
    void set_stream(int v, const uint8_t* data, uint32_t size);
    // Só no PC: para na hora as vozes que leem data (o buffer vai ser trocado ou liberado).
    void detach(const uint8_t* data);
    // Só no PC: a voz pede os blocos a src (nulo: volta à RAM, em ssa).
    void set_source(int v, Spu2BlockSource* src);
    void set_reverb_send(int v, bool on);        // VMIXEL/VMIXER
    void key_on(int v, int delay_samples = 0);   // KON (com atraso em amostras de 48 kHz)
    void key_off(int v);                         // KOFF
    uint16_t envx(int v) const;                  // SD_VP_ENVX: 0 = calada
    uint16_t pitch(int v) const;
    uint16_t volume_left(int v) const;
    uint16_t volume_right(int v) const;
    bool end_flag(int v) const;                  // ENDX: a voz passou por um bloco com o bit 0

    // Só o SNDDRV.IRX (ver o cabeçalho).
    void set_adsr(int v, uint16_t adsr1, uint16_t adsr2);  // SD_VP_ADSR1/2 (ataque e soltura lineares)
    void set_volume_reg(int v, int side, uint16_t reg);    // SD_VP_VOLL/R inteiro (bit 15: varredura)
    int16_t volume_current(int v, int side) const;         // SD_VP_VOLXL/R
    void set_loop_start(int v, uint32_t addr);             // SD_VA_LSAX
    uint32_t next_address(int v) const;                    // SD_VA_NAX
    void set_dry(int v, int side, bool on);                // VMIXL/VMIXR
    void set_mmix(int core, uint16_t mmix);                // SD_P_MMIX
    void set_reverb_type(int core, int type);              // o tipo do sceSdSetEffectAttr (0..9)
    uint64_t unsupported() const { return unsupported_; }  // taxas/modos do ADSR ou da varredura fora do portado

    static constexpr int kCores = 2;
    static constexpr int core_of(int v) { return v / 24; }
    void set_master_volume(uint16_t reg);              // MVOLL/R dos dois núcleos (o driver escreve sempre os dois)
    void set_master_volume(int core, uint16_t reg);    // MVOLL/R de um núcleo (modo fixo de 15 bits)
    void set_effect_volume(uint16_t reg);              // EVOLL/R dos dois núcleos
    void set_effect_volume(int core, uint16_t reg);    // EVOLL/R de um núcleo (16 bits com sinal)
    void set_input_volume(int core, uint16_t reg);     // BVOLL/R (a entrada de dados; 16 bits com sinal)
    uint16_t master_volume(int core) const { return mvol_[core]; }
    uint16_t effect_volume(int core = 0) const { return evol_[core]; }
    uint16_t input_volume(int core = 0) const { return bvol_[core]; }

    // A entrada de dados do núcleo 0 (a transferência em bloco dos filmes; só no PC é uma fila): amostras estéreo
    // intercaladas de 48 kHz.
    void input_push(const int16_t* stereo, size_t frames);
    void input_start();   // começa a consumir (audioDecStart)
    void input_stop();    // para, esvazia a fila e zera as contas (audioDecReset)
    bool input_active() const { return in_active_; }
    size_t input_queued() const { return (in_.size() - in_read_) / 2; }
    uint64_t input_consumed() const { return in_consumed_; }  // amostras tiradas da fila desde input_start
    uint64_t input_starved() const { return in_starved_; }    // amostras de silêncio com a fila vazia, ligada

    // Mistura frames amostras estéreo intercaladas (48 kHz) em out; out pode ser nulo (só avança as vozes).
    void render(int16_t* out, int frames);

    // Para o relatório e os testes.
    int voices_sounding() const;  // vozes com envelope > 0
    uint64_t samples_rendered() const { return rendered_; }

    // Só no PC (medida): quantas amostras (por canal) a SPU2 saturou em cada ponto da mistura desde o reset, e o maior
    // valor que a saída teria sem a saturação final (para dizer quanto falta de folga).
    struct Saturation {
        uint64_t voices[kCores] = {};  // a soma seca das vozes de um núcleo passou de 16 bits
        uint64_t core0 = 0;            // a saída do núcleo 0 (a entrada externa do 1)
        uint64_t output = 0;           // a saída final (o DAC)
        int32_t peak = 0;              // o maior |valor| antes da saturação final
    };
    const Saturation& saturation() const { return sat_; }

private:
    struct Voice {
        uint16_t vol_l = 0, vol_r = 0;
        uint16_t pitch = 0;
        uint32_t ssa = 0;
        const uint8_t* ext = nullptr;  // buffer de stream (PC)
        Spu2BlockSource* source = nullptr;  // fonte de blocos (PC: a música)
        uint32_t ext_size = 0;
        bool reverb = false;
        // estado
        bool on = false;
        bool releasing = false;
        bool endx = false;
        int key_delay = -1;  // amostras até o key on (>= 0: pendente)
        int32_t env = 0;
        uint32_t addr = 0;       // próximo bloco a decodificar (bytes)
        uint32_t loop_addr = 0;
        bool stop_after = false; // o bloco atual tem fim sem repetição
        AdpcmState adpcm;
        int16_t block[28] = {};
        int block_pos = 28;      // próxima amostra de block
        int16_t hist[4] = {};    // as quatro últimas amostras (interpolação)
        uint32_t counter = 0;    // fração de 12 bits
        // SNDDRV.IRX (os padrões dão o comportamento antigo)
        int32_t attack_step = 7 << 11;
        int32_t release_step = 8 << 11;
        bool loop_fixed = false;   // LSAX escrito: o key on não o troca pelo SSA
        bool dry[2] = {true, true};
        bool sweep[2] = {false, false};
        uint16_t sweep_reg[2] = {};
        int32_t sweep_level[2] = {};   // o módulo (0..0x7fff)
        uint32_t sweep_wait[2] = {};   // amostras até o próximo passo
    };
    // O reverb de um núcleo: buffer circular de 24 kHz (o hardware roda o reverb a cada 2 amostras).
    struct Reverb {
        std::vector<int16_t> buf;
        uint32_t pos = 0;
        int32_t in_l = 0, in_r = 0;
        int32_t out_l = 0, out_r = 0;
    };
    const uint8_t* voice_data(const Voice& vc, uint32_t addr) const;
    uint32_t voice_size(const Voice& vc) const;
    void start_voice(Voice& vc);
    int16_t next_sample(Voice& vc);
    void reverb_process(Reverb& rv, int core, int32_t in_l, int32_t in_r);
    int32_t voice_gain(Voice& vc, int side);

    Voice voices_[kVoices];
    uint16_t mvol_[kCores] = {};
    uint16_t evol_[kCores] = {};
    uint16_t avol_[kCores] = {};  // entrada externa (AVOL): 0 no núcleo 0, 0x7fff no 1 (o sceSdInit)
    uint16_t bvol_[kCores] = {};  // entrada de dados (BVOL): 0 (o sceSdInit); os filmes mudam o do núcleo 0
    std::vector<int16_t> in_;     // a fila da entrada de dados do núcleo 0
    size_t in_read_ = 0;
    bool in_active_ = false;
    uint64_t in_consumed_ = 0;
    uint64_t in_starved_ = 0;
    uint16_t mmix_[kCores] = {};  // MMIX: o que entra no seco e no reverb de cada núcleo (0xff0, 0xffc)
    uint64_t rendered_ = 0;
    Reverb rev_[kCores];
    bool rev_phase_ = false;
    Saturation sat_;
    int rev_type_[kCores] = {4, 4};  // o preset do reverb de cada núcleo (4: o "studio C" do SFX.IRX)
    uint64_t unsupported_ = 0;
    float dc_x_[2] = {0.0f, 0.0f};  // bloqueio de DC: a entrada e a saída anteriores
    float dc_y_[2] = {0.0f, 0.0f};
};

}  // namespace nf
