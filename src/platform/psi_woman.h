#pragma once
// O fundo animado da pausa (a "woman" do PS2; docs/port.md "O fundo animado da pausa", docs/cxbx.md 3.1).
//
// O painel da pausa e do fim de missão desenha a textura 0x030000db (o rótulo de imagem 0x10000184, aditivo, alfa 0x50).
// No PS2 ela não é a imagem guardada no pacote (luzes de festa e um homem com uma taça): na carga, o
// CreateSpaceAndPackTextures (0x0011dcf8) troca a paleta dela pela da textura de fogo 0x030000da e zera o espaço dela na
// VRAM (o desenho guardado nunca aparece); com o jogo pausado, o Game_Run chama psiDecompressWoman, que um tick sim, um
// não (a 60 quadros), decodifica no scratchpad um quadro de um RLE de 128 x 128 (o item tipo 16 do pacote, que o
// PS2SetUpWoman guarda): trechos zerados e trechos copiados da textura de fogo, que rola com WomanFrame. O resultado é a
// silhueta de uma mulher com uma pistola, cheia de fogo, como nas aberturas dos filmes do 007: 348 quadros (11,6 s a 60
// quadros), e o quadro 348 volta ao primeiro (o Xbox segura o último 240 ticks; o PS2 não).
//
// No PC (só do port a forma): os pixels e a paleta da textura são as cópias no heap do pstex_tag (o port não monta os
// espaços da VRAM: o renderizador tem uma textura por entrada de Tex). O fogo é lido das cópias dele: no PS2 ele fica
// sozinho no seu espaço de 128 x 256 (o Reduce abre um espaço do tamanho de VRAM da maior textura, e a de 128 x 256 o
// ocupa inteiro), sem o Conv8to32 (o CreateSpaceAndPackTextures o pula de propósito), então o espaço é a textura linear.
// O Conv8to32 (0x0011eeb0) arruma os bytes de 8 bits para subirem como PSMCT32 e caírem na VRAM como PSMT8: o que o GS lê
// é a imagem linear; no PC a imagem linear vai para os pixels da textura e o backend gráfico troca a textura
// (PsiGfxBackend::update_texture, Renderer::update_texture).
//
// Divergências (contadas em PsiWomanStats::guards, todas zero nos dados do jogo): sem o item da "woman", sem uma das duas
// texturas ou com tamanhos que não são os do disco (o PS2 leria fora: o 0x030000db sem nulo conferido), e um RLE que
// passaria do fim do quadro ou do item, a decodificação para. PS2FramesToSkip 0 (o port não o escreve) vale 1.

#include <cstddef>
#include <cstdint>

// O item tipo 16 do pacote (LoaderProcess): os quadros do RLE.
void PS2SetUpWoman(uint8_t* data);
// Só do port: o fim do item (o LoaderProcess o passa com o DirFileLen), para as guardas da leitura.
void psi_woman_set_end(const uint8_t* end);
// Um tick com o jogo pausado (Game_Run).
void psiDecompressWoman();
// O trecho do CreateSpaceAndPackTextures (psiPreGameRun) que prepara a 0x030000db: a paleta do fogo e os pixels zerados.
void psi_woman_pack_textures();

// O estado do RLE (os nomes do PS2).
extern uint8_t* pWoman;      // 0x0030d704 o próximo byte do RLE
extern uint8_t* PS2Woman;    // 0x0030d700 o começo (o item)
extern uint8_t* WomanBuf;    // 0x0030d6fc o scratchpad (0x70000000 no PS2; um buffer de 16 KB no PC)
extern uint32_t WomanFrame;  // 0x0030d708 o quadro (0 a 347)

constexpr uint32_t kWomanFireTexture = 0x030000da;
constexpr uint32_t kWomanTexture = 0x030000db;
constexpr int32_t kWomanBytes = 0x4000;   // 128 x 128, 8 bits por pixel
constexpr uint32_t kWomanFrames = 0x15c;  // 348

struct PsiWomanStats {
    uint32_t decoded = 0;   // quadros decodificados desde o boot
    uint32_t updates = 0;   // trocas de textura pedidas ao backend (os quadros e o preparo da carga)
    uint32_t packs = 0;     // preparos da carga (psi_woman_pack_textures que acharam as duas texturas)
    uint32_t guards = 0;    // casos em que o PS2 leria fora (ver o cabeçalho)
    int32_t last_frame = -1;  // o último quadro decodificado (-1: nenhum desde a carga)
    uint32_t item_bytes = 0;  // o tamanho do item (psi_woman_set_end; 0: desconhecido)
    uint32_t cycle_bytes = 0; // os bytes do RLE lidos no último ciclo completo (até o quadro 348 voltar ao começo)
    int32_t texture = -1;     // a entrada de Tex da 0x030000db (-1: sem ela)
};
extern PsiWomanStats g_psi_woman;

// A linha "fundo_pausa ..." do relatório das capturas.
void psi_woman_describe(char* out, size_t size);
// Só para testes: o estado do boot (skipit.288 = 2, nada carregado).
void psi_woman_debug_power_on();
