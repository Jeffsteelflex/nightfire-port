#pragma once
// As legendas das missões de carro (GSubtitles e GSubtitleSplitter, GHud.obj do DRIVING.ELF; docs/driving.md seção
// 28): as dos filmes (os .sub de data\loading: "N" linhas "FRAME_<quadro>=<rótulo do .loc>", 0 apaga) e as do jogo
// (DisplaySubtitle). O texto vai em pedaços de até 52 caracteres (WordWrap), cada um por um tempo proporcional ao seu
// tamanho, desenhados na fonte data\loading\loading.sfn (a biblioteca FONT da EA, driving/render/ea_font.h) centrados
// 67 pixels acima do pé da tela, com a cor 0x7fffffff (o alfa a meio).
//
// Portado do MIPS do PS2 com o leiaute do PS2 (GSubtitlesData 0x94, GSubtitleSplitterData 0x64); conferido na parte 24
// do nf_driving_check. Divergências (só do port): a fonte e o arquivo do .sub ficam com o port (o +0xc e o +0x10
// guardam 1 quando carregados); o FONT_drawtextfa formata o texto sem argumentos (o port o passa como está); a
// GetFoundationVideoMode é a do NTSC (1); um SetupFMVSubtitles com a tabela já liberada (o ClearFrames não zera a
// contagem) leria o endereço 0: guardado e contado.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "driving/render/ea_font.h"
#include "platform/ptr32.h"

namespace drv {

// GSubtitleSplitter (0x64).
struct GSubtitleSplitterData {
    P32<char> lines[10];      // +0x00 os pedaços (operator new)
    float durations[10];      // +0x28 os segundos de cada um
    uint32_t count = 0;       // +0x50
    uint32_t current = 0;     // +0x54
    float duration = 0;       // +0x58 o tempo total
    int32_t start = 0;        // +0x5c o relógio do começo do pedaço
    int32_t game_clock = 0;   // +0x60 1: o relógio do jogo (o passo), 0: o TIMER
};
static_assert(sizeof(GSubtitleSplitterData) == 0x64);

// GSubtitles (0x94).
struct GSubtitlesData {
    int32_t game_clock = 0;   // +0x00
    float duration = 0;       // +0x04
    int32_t start = 0;        // +0x08
    uint32_t font = 0;        // +0x0c o FONT (port: 1 carregada)
    uint32_t file = 0;        // +0x10 o arquivo da fonte (port: 1)
    float width = 640.0f;     // +0x14 o RRenderer +0x50
    float height = 448.0f;    // +0x18 o RRenderer +0x54
    P32<int32_t> frames;      // +0x1c pares {quadro, rótulo}
    uint32_t index = 0;       // +0x20
    uint32_t count = 0;       // +0x24
    int32_t label = 0;        // +0x28 o rótulo mostrado (0 nenhum)
    GSubtitleSplitterData splitter;  // +0x2c
    uint32_t vtable = 0;      // +0x90
};
static_assert(sizeof(GSubtitlesData) == 0x94);

// O GSubtitles do port (o fTheApp) e a fonte dele.
GSubtitlesData* GSubtitles_TheApp();
EaFont& gsubtitles_font();
// Quem lê os arquivos (o UFileLoader::FileLoad do MISC.VIV) e dá o número da imagem da fonte.
struct SubtitleFiles {
    std::function<bool(const std::string& name, std::vector<uint8_t>& out)> load;
    std::function<uint32_t(const std::vector<uint8_t>& font_file)> image;  // a imagem decodificada da fonte (o port)
};
// O GSubtitles do jogo (o RunTheGame o cria na pilha antes do filme de abertura; o port o guarda aqui).
GSubtitlesData& gsubtitles_storage();
SubtitleFiles& subtitle_files();

// [driving] 0x0024a0d8 GSubtitles::GSubtitles (a fonte data\loading\loading.sfn)
void GSubtitles_Construct(GSubtitlesData& s);
// [driving] 0x0024a168 GSubtitles::~GSubtitles
void GSubtitles_Destruct(GSubtitlesData& s);
// [driving] 0x0024a290 GSubtitles::LoadFont
void GSubtitles_LoadFont(GSubtitlesData& s, const char* name);
// [driving] 0x0024a4d0 GSubtitles::UnloadFont
void GSubtitles_UnloadFont(GSubtitlesData& s);
// [driving] 0x0024a300 GSubtitles::LoadSubtitles (o texto do arquivo: o port o recebe já lido; vazio = não carregou)
void GSubtitles_LoadSubtitles(GSubtitlesData& s, const char* name);
void GSubtitles_LoadSubtitlesText(GSubtitlesData& s, std::string text);
// [driving] 0x0024a518 GSubtitles::UnloadSubtitles
void GSubtitles_UnloadSubtitles(GSubtitlesData& s);
// [driving] 0x0024a550 GSubtitles::ClearFrames
void GSubtitles_ClearFrames(GSubtitlesData& s);
// [driving] 0x0024a870 GSubtitles::ClearSubtitles
void GSubtitles_ClearSubtitles(GSubtitlesData& s);
// [driving] 0x0024a1e0 GSubtitles::IsSubtitleDisplaying
bool GSubtitles_IsSubtitleDisplaying(const GSubtitlesData& s);
// [driving] 0x0024a1f0 GSubtitles::DisplaySubtitle(unsigned int rótulo, float segundos) (o SMissionManager::
// GoingToSkipCinematic pelo `skipping`)
void GSubtitles_DisplaySubtitle(GSubtitlesData& s, int32_t label, float seconds, bool skipping);
// [driving] 0x0024a598 GSubtitles::SetupFMVSubtitles
void GSubtitles_SetupFMVSubtitles(GSubtitlesData& s, uint32_t frame);
// [driving] 0x0024a6d0 GSubtitles::DrawSubtitles: as legendas no pacote (nada se não há legenda).
void GSubtitles_DrawSubtitles(GSubtitlesData& s, GsPacket& out);
// [driving] 0x0024a088 DrawSubtitlesCallback(unsigned int quadro)
void DrawSubtitlesCallback(uint32_t frame, GsPacket& out);

// [driving] 0x0024ac40 GSubtitleSplitter::GSubtitleSplitter
void GSubtitleSplitter_Construct(GSubtitleSplitterData& p);
// [driving] 0x0024ac58 GSubtitleSplitter::Init
void GSubtitleSplitter_Init(float seconds, GSubtitleSplitterData& p, const char* text, int32_t game_clock);
// [driving] 0x0024acc0 GSubtitleSplitter::Reset
void GSubtitleSplitter_Reset(GSubtitleSplitterData* p);
// [driving] 0x0024ad40 GSubtitleSplitter::GetString
const char* GSubtitleSplitter_GetString(GSubtitleSplitterData& p);
// [driving] 0x0024a9c0 GSubtitleSplitter::WordWrap
int32_t GSubtitleSplitter_WordWrap(GSubtitleSplitterData& p, const char* text, uint32_t max, GSubtitleSplitterData& out);

struct SubtitleStats {
    uint64_t loads = 0, shown = 0, drawn = 0, guards = 0;
};
SubtitleStats& subtitle_stats();

}  // namespace drv
