// Funções da plataforma ainda não implementadas no PC. Cada uma só registra a chamada
// (platform/pending.h).

#include "platform/pending.h"
#include "platform/psi.h"
#include "platform/psi_gfx.h"
#include "platform/psi_gfx.h"

int32_t PS2ScreenXPos = 0;
int32_t PS2ScreenYPos = 0;
uint8_t PS2UpdateScreenPos = 0;

// [decomp] 0x0010b890 psiAdjustScreenPos
// Um passo do ajuste de tela (P_SCREENADJUST, C_SBSCREEN_Handler): x de 12 em 12 até ±0x1b0, y de 4 em 4 até ±0x30.
// No PC só guarda (ver psiSetScreenPos).
void psiAdjustScreenPos(int32_t dx, int32_t dy) {
    PS2ScreenYPos += dy * 4;
    PS2ScreenXPos += dx * 0xc;
    if (0x1b0 < PS2ScreenXPos) PS2ScreenXPos = 0x1b0;
    if (PS2ScreenXPos < -0x1b0) PS2ScreenXPos = -0x1b0;
    if (0x30 < PS2ScreenYPos) PS2ScreenYPos = 0x30;
    if (PS2ScreenYPos < -0x30) PS2ScreenYPos = -0x30;
    PS2UpdateScreenPos = 1;
}

// [decomp] 0x0010b910 psiSetScreenPos
// No PS2 o próximo quadro move a imagem na TV (PS2UpdateScreenPos, o DISPLAY do GS); no PC a imagem não tem margem
// de TV a compensar e a janela já mostra tudo: a posição só é guardada (o perfil a grava no bloco GSET). Divergência
// decidida com o usuário: nada no desenho a lê (nem o 3D nem a camada 2D).
void psiSetScreenPos(int32_t x, int32_t y) {
    PS2ScreenXPos = x;
    PS2ScreenYPos = y;
    PS2UpdateScreenPos = 1;
}

// [decomp] 0x0010b928 psiGetScreenPos
void psiGetScreenPos(int32_t* x, int32_t* y) {
    *x = PS2ScreenXPos;
    *y = PS2ScreenYPos;
}

// [decomp] 0x0010b940 psiRestoreScreenPos
// A posição padrão ("Restore Defaults" da página AV Options).
void psiRestoreScreenPos() {
    PS2ScreenXPos = 0;
    PS2UpdateScreenPos = 1;
    PS2ScreenYPos = 0;
}

void psiDecompressWoman() { pending_call("psiDecompressWoman"); }
void psiLaunchDriving(uint8_t*, uint32_t) { pending_call("psiLaunchDriving"); }

// [decomp] 0x00109520 psiPreGame_Run
// No PS2 também cria o buffer de desenho (CreateDB) e conta o tempo da lógica para o perfil.
void psiPreGame_Run() {
    if (PS2FirstGameRun != 0) {
        PS2WipeVram();
        PS2FirstGameRun = 0;
    }
    SFXUpdate();
}

// [decomp] 0x00109580 psiPostGame_Run
// No PS2 também fecha a contagem de tempo da lógica.
void psiPostGame_Run() { SFXUpdate(); }
