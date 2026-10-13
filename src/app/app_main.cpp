// Nightfire PC: o laço do nightfire.exe, sem nada do sistema (docs/port.md, "Plataforma: interfaces e backends"). O
// ponto de entrada é o do backend (platform/entry_win32.cpp: wWinMain no Windows), que chama nf_app_main com a linha de
// comando; a janela, o renderizador, o som, os filmes e o resto do sistema vêm das interfaces de src/platform e
// src/render (os backends do Windows: Win32, Direct3D 11, Direct2D, XAudio2, Media Foundation).
//
// Lê ACTION.ELF + FILES.BIN da cópia do usuário (platform/disc_source.h: a pasta com os arquivos extraídos, a imagem
// .iso, o disco num leitor de DVD ou a cópia pessoal dentro do próprio executável) e roda as fases com Direct3D 11, em
// qualquer resolução e proporção, com mouse e teclado. Antes do jogo, a tela de entrada (o launcher, render/setup_app.h e
// ui/modern_setup.h): a cada partida sem argumentos (além de --config e --save-dir), com o Shift seguro quando ela está
// desligada ([ui] launcher), ou sem um disco que sirva (então ela pede a imagem do disco).
//
// Controles: clique captura o mouse, Esc solta (num menu ele fica solto e volta à captura quando o menu
// fecha). Jogando (o Bond): mouse olha, WASD anda, Espaço pula, C ou Ctrl agacha, E usa (portas...), botão direito mira; o resto do mapeamento está em
// platform/psi_input.cpp. F2 alterna para a câmera livre (WASD move, Q/E desce/sobe, Shift acelera,
// Ctrl desacelera, roda ajusta a velocidade, Espaço abre ou fecha a porta à frente). Sempre:
// PgUp/PgDn troca de fase; V cores de vértice; F3 wireframe; H mostra gatilhos e marcadores;
// P desenha todas as salas (switch_ForceDrawAll, sem a visibilidade por portais); T alterna as
// texturas HD; F4 alterna a camada 2D em alta definição; F12 salva captura; Alt+Enter tela cheia.
//
// A lógica roda pelo GameFlow_Main portado, em ticks de 60 Hz como no PS2. A câmera 0 do jogo é a
// do jogador (Player_PositionCamera, atualizada a cada tick); na câmera livre o main põe a matriz
// dela no viewer_tag a cada quadro de vídeo. O Game_Draw roda uma vez por tick (como no PS2: o estado que ele
// guarda anda no ritmo do jogo) e o registro do que ele pediu (psi_draw, RuntimeGfx::collect_frame) vai para a
// interpolação entre ticks (render/frame_interp.h): cada quadro de vídeo desenha entre os dois últimos registros,
// e o mouse gira a visão sem o tick de atraso. Com --no-interp, o Game_Draw a cada quadro de vídeo, como antes.
//
// Linha de comando:
//   --disc <caminho>      o disco do jogo: uma imagem .iso, a pasta com os arquivos extraídos (ACTION.ELF, FILES.BIN,
//                         PS2\, MOVIES\...), um leitor de DVD ("D:") ou um executável com a cópia pessoal. Sem ele: a
//                         cópia pessoal do próprio executável, a pasta dele (os arquivos ou uma .iso), a pasta atual e as
//                         de cima do executável (o repositório), o [game] disc do nightfire.ini e os leitores de DVD;
//                         sem nenhum que sirva, a tela de configuração. Só a versão americana (SLUS-20579) serve
//   --data <pasta>        o mesmo que --disc (o nome antigo)
//   --setup, --launcher   mostra a tela de entrada (o disco, a cópia, a resolução, o modo de tela, o VSync, o estilo dos
//                         menus, a camada 2D em HD, "Show at startup"). Sem argumentos (além de --config e --save-dir)
//                         ela aparece sozinha, a não ser que [ui] launcher = off (então o Shift seguro na partida a traz);
//                         qualquer outro argumento a pula (as capturas, --level, --disc...), menos sem um disco que sirva
//   --setup-pick <caminho> capturas: a resposta da janela de escolha de arquivo da tela de entrada
//   --setup-input "<seq>" capturas: o roteiro da tela de entrada (como --input; o relógio conta da tela)
//   --pack-disc <caminho> faz a cópia pessoal em um arquivo só (platform/disc_pack.h): este executável com os arquivos do
//                         disco dentro (ACTION.ELF, SYSTEM.CNF, FILES.BIN, PS2\ e MOVIES\) e sai. O arquivo contém o jogo
//                         do usuário: é só para ele, não pode ser compartilhado. Nunca dentro de um repositório git
//   --out <arquivo.exe>   com --pack-disc: onde gravar (padrão: <executável>-personal.exe ao lado dele)
//   --pack-no-movies      com --pack-disc: sem os filmes (uns 850 MB a menos)
//   --level <hash>        começa direto nessa fase (sem o menu principal); sem ele, o menu principal do jogo
//                         (07000048: idioma, cartão, "Press START", o menu, os codinomes e o mapa das missões)
//   --save-dir <pasta>    a pasta do cartão de memória (os perfis; padrão: saves\ ao lado do executável). Com
//                         --screenshot e sem --save-dir o cartão fica em memória (as capturas não tocam nos saves)
//   --width/--height      tamanho da janela (padrão 1600x900)
//   --fullscreen          começa em tela cheia sem bordas
//   --fov <graus>         campo de visão vertical (padrão 60; o horizontal cresce com a largura)
//   --sensitivity <x>     radianos por contagem do mouse (padrão 0.0022)
//   --at x,y,z,giro       põe o jogador de pé nesse ponto (no chão abaixo dele), virado para o giro
//                         em radianos (depuração; nf_loader_check mostra pontos em porta_usada)
//   --input "<seq>"       entrada roteirizada do jogador para capturas: trechos "teclas:segundos"
//                         separados por espaço, cada um segura as teclas (unidas por +) por esse
//                         tempo; teclas são letras, dígitos, space, left, right, up, down, ctrl, lmb,
//                         rmb, enter, back e tab. Ex.: --input "left:0.5 W:2 E:0.1". Com um menu aberto o
//                         roteiro também vale (Enter abre e fecha a pausa, Espaço escolhe, setas andam).
//                         Outras teclas pelos nomes do nightfire.ini (escape, delete, f5, mouse4...) e um
//                         controle simulado: pad_a, pad_b, pad_x, pad_y, pad_lb, pad_rb, pad_lt, pad_rt,
//                         pad_start, pad_back, pad_ls, pad_rs, pad_up/down/left/right e os analógicos
//                         pad_lx=v, pad_ly=v, pad_rx=v, pad_ry=v (-1 a 1, y para cima; também pad_rt=v).
//                         Ex.: --input "pad_ly=1:2 pad_rt:0.5" anda 2 s e atira com o controle; pad_off desconecta
//                         o controle simulado no trecho (o aviso de controle desconectado do jogo)
//                         O mouse dos menus: mouse=x,y põe o cursor nesse ponto da tela do PS2 (512 x 448, as
//                         coordenadas de texto2d no relatório) a partir do trecho, até outro mouse=; wheel=n gira
//                         n dentes (positivo para cima) no começo do trecho; lmb, rmb e mouse4 são os botões.
//                         Ex.: --input "none:1 enter:0.2 none:0.5 mouse=300,250:0.3 mouse=300,250+lmb:0.1"
//   --open-menu-page <id> depuração: abre a pausa e mostra a página (hex; 40000060 as opções do PC) meio
//                         segundo depois da carga
//   --menu-tab <aba>      com --open-menu-page 40000060: a aba (video, graphics, interface, audio, controls, keys;
//                         no estilo Modern também gameplay e controller)
//   --menu-style <s>      o estilo dos menus do PC: modern (padrão; src/ui, docs/design.md) ou original (as páginas
//                         do jogo); também [ui] menu_style no nightfire.ini
//                         No estilo Modern, cmouse=x,y no --input põe o cursor num ponto da tela virtual de
//                         1920 x 1080 (as coordenadas de docs/design.md).
//   --novsync
//   --fps-cap <n|off>     limite de quadros por segundo ([video] fps_cap; off ou unlimited: sem limite)
//   --no-interp           sem a interpolação entre ticks ([video] interpolation = off): o comportamento antigo
//   --no-instant-mouse    sem o olhar do mouse sem atraso ([video] instant_mouse_look = off)
//   --aim-style <s>       o estilo de mira ([game] aim_style): auto (padrão: moderno com teclado e mouse, o do jogo
//                         com o controle), modern ou classic (engine/player_ads.h)
//   --wheel-slowmo <f>    a lógica com a roda de armas aberta ([game] wheel_slowmo: 0.25 padrão, 1 tempo real, 0
//                         parada; engine/weapon_wheel.h)
//   --fixed-video-hz <n>  capturas: um monitor simulado de n Hz (o relógio do jogo avança 60 / n VBlanks por
//                         quadro de vídeo), para quadros entre ticks determinísticos
//   --frames-dump <pasta> --frames <n>  capturas: n quadros seguidos a partir da captura (frame_NNN.png e
//                         frames.tsv; --frames-no-png só o .tsv)
//   --interp-alpha <a>    depuração: o alpha da interpolação fixo (0 o tick anterior, 1 o atual)
//   --only-2d             depuração: só o 2D (HUD, menus, legendas)
//                         No --input, mlook=dx,dy olha com o mouse (contagens por segundo durante o trecho).
//   --screenshot <png>    renderiza alguns quadros, salva a imagem e sai (fora da tela, no tamanho de
//                         --width/--height, mesmo maior que a área de trabalho)
//   --cam x,y,z,tx,ty,tz  começa na câmera livre com essa posição e alvo (sem jogador na fase, a
//                         câmera livre começa numa visão geral)
//   --drawall             começa desenhando todas as salas (tecla P)
//   --use                 usa o que está à frente uma vez no começo (para capturas com --screenshot)
//   --textures <pasta>    pacote de texturas HD no formato do PCSX2 (padrão:
//                         <dados>\textures\SLUS-20579\replacements, se existir)
//   --no-hd               começa com as texturas originais (tecla T alterna)
//   --wait <segundos>     com --screenshot, captura depois desse tempo (ticks de 60 Hz desde a carga,
//                         contando também com o jogo pausado num menu)
//   --shot-level <hash>   com --screenshot, só captura nesse pacote (o --wait conta da carga dele):
//                         por exemplo 07000048, o menu principal com o resultado da missão; <hash>:<n> só na
//                         n-ésima carga dele (sem --level, 07000048:2 é a volta ao menu depois da missão)
//   --unlock-all          depuração: os perfis padrão como se todas as missões tivessem sido ganhas com platina
//                         (as missões liberadas, as recompensas do dossiê, as melhorias; engine/menu_cheats.h)
//   --debug-menu          depuração: F9 numa fase abre a pausa na página de depuração do jogo (0x40000007: as
//                         trapaças C_CHCH*, que a versão de venda não alcança); nas capturas, --open-menu-page 40000007
//   --win <segundos>      depuração: liga o canal 100 (todos os objetivos cumpridos) esse tempo depois
//                         da carga da primeira fase fora do menu principal, para capturar o fim da missão e o
//                         resultado
//   --channel <hex>       depuração: liga esse canal (switch_channels) logo depois da carga da primeira fase fora do
//                         menu principal; repetível (capturas dos holofotes e câmeras que esperam um canal da história)
//   --weapon <hex>        depuração: dá a arma (weapon_data, com um pente e 100 balas) e a pede na mão logo depois da
//                         carga (capturas da mira moderna de cada arma; a pele precisa estar carregada na fase). Uma
//                         lista separada por vírgulas dá todas (a primeira na mão); "all" dá todas as armas e aparelhos
//                         da troca de armas, como a trapaça (as capturas da roda de armas com muitas armas)
//   --no-lights          sem as luzes da VU1 (depuração: as rotinas de luz viram as sem luz)
//   --no-morphs           sem os morphs dos rostos (depuração: a pose de repouso, como antes do port deles)
//   --no-torso-aim        sem a mira do tronco (depuração: AnimObjectAimAt não faz nada, como antes do port dela)
//   --no-fog              sem a passada de névoa (depuração)
//   --debug-shading       começa com o sombreamento de depuração (tecla V alterna)
//   --no-subtitles        sem legendas (a opção do menu de opções, DrawInfo[2]; o port começa ligada)
//   --no-audio            sem som (o padrão das capturas com --screenshot; o jogo e o driver do IOP rodam iguais e a
//                         mistura é descartada: platform/audio_out.h)
//   --audio-dump <wav>    grava o som misturado num WAV de 48 kHz estéreo (na linha do tempo do jogo, do boot até
//                         sair), sem tocar; serve com --screenshot para medir o som sem caixas (tools/sound.py analyse)
//   --hd-ui               começa com a camada 2D em alta definição (tecla F4 alterna; render/ui_hd.h)
//   --hd-ui-dir <pasta>   pasta com map.json, substitutas e fonts/ (padrão: hd_ui do repositório)
//   --hd-ui-anchor <a>    centre (como o original) ou edges (HUD preso às bordas); padrão o do map.json
//   --ui-inventory <pasta> inventário das imagens 2D desenhadas (inventory.tsv e recortes em PNG)
//   --prompts <p>         símbolos de botão da camada HD: auto (o último aparelho usado, padrão), ps2,
//                         keyboard (as teclas ligadas) ou xbox; também [ui] prompts no nightfire.ini
//   --config <ini>        o arquivo de opções (padrão: nightfire.ini ao lado do executável)
//   --window-mode <m>     windowed, borderless (o mesmo que --fullscreen) ou exclusive (tela cheia do DXGI)
//   --render-scale <pct>  a cena 3D em pct % do tamanho da janela (50 a 200; o 2D fica na janela)
//   --msaa <n>            MSAA da cena 3D com 2, 4 ou 8 amostras (1: desligado); com ele, os recortes do teste de alfa
//                         (folhas, grades) vão com o alfa para a cobertura (alpha-to-coverage)
//   --no-alpha-coverage   depuração: com MSAA, o teste de alfa descarta o pixel inteiro, como sem MSAA (para comparar)
//   --filter <f>          filtro das texturas: bilinear, trilinear, aniso2, aniso4, aniso8 (padrão), aniso16
//   --lang-pc <l>         idioma dos textos só do port (as opções do PC no menu): en (padrão) ou pt
//   --adapter <a>         o adaptador de vídeo: auto (padrão: o do sistema), integrated, discrete, o índice do
//                         --list-adapters ou um trecho do nome ("radeon"); também [video] adapter (platform/gpu_adapter.h)
//   --list-adapters       escreve os adaptadores (índice, nome, fabricante, VRAM, integrado ou dedicado) e sai
//   --bench <s>           benchmark (docs/port.md "Benchmark"): a fase sem limite de quadros nem VSync por s segundos de
//                         relógio, depois de --wait (padrão 1 s) desde a carga; escreve quadros, fps, tempos de quadro
//                         (média, 1% e 0,1% low), o tempo de GPU por trecho (consultas de carimbo de tempo), os custos de
//                         CPU, os desenhos e as opções. Na janela (com o Present), ou fora da tela com --offscreen. Como as
//                         capturas: só o --input move o jogador e o nightfire.ini só é lido com --config
//   --bench-out <txt>     o resultado do --bench também nesse arquivo (acrescentado)
//   --bench-frames <tsv>  com --bench: uma linha por quadro medido (o tempo do quadro, do envio e do Present, em ms)
//   --bench-vsync         com --bench: mantém o VSync e o limite de quadros das opções (o ritmo de quem joga)
//   --offscreen           com --bench: desenha numa textura do tamanho de --width/--height, sem janela visível no
//                         desenho nem Present (o custo da GPU sem a cópia para o monitor)
//   --movies / --no-movies os filmes (MOVIES\30_FPS\*.PSS; platform/psi_movie.h): ligados por padrão ([video] movies),
//                         desligados nas capturas e no --bench a não ser com --movies (sem eles, as páginas de filme
//                         seguem na hora, como antes)
//   --movie-decoder <d>   o decodificador de MPEG-2 dos filmes (platform/movie_decoder.h): auto (padrão: o do sistema,
//                         o Media Foundation, e o do port se ele não servir), mf (só o do sistema) ou own (só o do
//                         port, platform/mpeg2_decoder.h); também [video] movie_decoder
//   --movie-frame <n>     capturas: liga os filmes e captura no quadro n do filme que tocar (com --movie <hash>, só
//                         desse); o --wait não conta
//   --intro-movie         com --level: a fase pedida com o filme de abertura (07100001 para 07000001...), como o mapa
//                         das missões a pede
// As opções do PC também ficam no menu de pausa (Enter): OPTIONS, uma página em abas (engine/menu_pc_options.h),
// gravadas em nightfire.ini ([video], [ui], [controls], [bindings]). Nos menus o mouse fica solto e funciona
// (engine/menu_mouse.h): passar por cima, clicar, a roda, o botão direito volta.

// A matemática da câmera livre e das matrizes de visão (só cabeçalhos, MIT: github.com/microsoft/DirectXMath). Fora do
// Windows, o pedaço dele que este laço usa, com as contas do caminho sem intrínsecos (app/xmath_compat.h).
#if defined(_WIN32)
#include <DirectXMath.h>
#else
#include "app/xmath_compat.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "data/files_bin.h"
#include "platform/arena.h"
#include "data/texture_pack.h"
#include "engine/break.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/trigger.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/hashtable.h"
#include "engine/hazard.h"
#include "engine/hud.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/menu_pc_options.h"
#include "engine/pc_text.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/pickup.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/player_ads.h"
#include "engine/weapon_wheel.h"
#include "engine/player_stats.h"
#include "engine/effect.h"
#include "engine/emitter.h"
#include "engine/explode.h"
#include "engine/menu.h"
#include "engine/menu_cheats.h"
#include "engine/menu_dossier.h"
#include "engine/menu_frontend.h"
#include "engine/menu_mouse.h"
#include "engine/menu_pc_controls.h"
#include "engine/resetmap.h"
#include "engine/scenery.h"
#include "engine/script.h"
#include "engine/rigid_body.h"
#include "engine/sensor.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weather.h"
#include "game/boot.h"
#include "game/level_scene.h"
#include "platform/audio_out.h"
#include "platform/bench_stats.h"
#include "platform/gpu_adapter.h"
#include "platform/disc_pack.h"
#include "platform/disc_source.h"
#include "platform/entry.h"
#include "platform/game_options.h"
#include "platform/gamepad.h"
#include "platform/input_bindings.h"
#include "platform/keys.h"
#include "platform/os.h"
#include "platform/crt_compat.h"
#include "platform/pc_gameplay.h"
#include "platform/psi.h"
#include "platform/psi_2d.h"
#include "platform/psi_card.h"
#include "platform/psi_draw.h"
#include "platform/psi_input.h"
#include "platform/psi_woman.h"
#include "platform/sfx_iop.h"
#include "platform/settings.h"
#include "platform/video_settings.h"
#include "platform/window.h"
#include "render/frame_interp.h"
#include "render/gpu_adapters.h"
#include "render/remix_d3d9.h"
#include "render/renderer.h"
#include "render/runtime_gfx.h"
#include "render/setup_app.h"
#include "render/ui_hd_app.h"
#include "render/ui_modern_app.h"
#include "ui/modern_input.h"
#include "ui/modern_setup.h"
#include "ui/modern_wheel.h"
#include "platform/fmath.h"

using namespace nf;
using namespace DirectX;

namespace {

// O estilo de menu Modern (o gancho da linha OPTIONS da pausa chama sem captura).
ModernUiApp* g_modern_app = nullptr;
// A roda de armas (só do port; engine/weapon_wheel.h): a lógica a cada quadro de vídeo, o desenho Modern por cima do jogo e
// a câmera lenta do agendador dos ticks.
WeaponWheel g_wheel;

struct Options {
    std::wstring disc;      // --disc / --data (vazio: a procura de platform/disc_source.h)
    std::wstring disc_ini;  // [game] disc do nightfire.ini
    bool setup = false;           // --setup, --launcher
    bool show_launcher = true;    // [ui] launcher: a tela de entrada a cada partida ("Show at startup")
    bool command_line = false;    // a linha de comando diz o que rodar (ui/modern_setup.h, launcher_command_line)
    std::wstring setup_pick;      // --setup-pick
    std::string setup_input;      // --setup-input
    std::wstring pack_disc;       // --pack-disc
    std::wstring pack_out;        // --out
    bool pack_movies = true;      // --pack-no-movies
    std::string level;      // --level (vazio: o menu principal)
    std::wstring save_dir;  // --save-dir
    int width = 1600, height = 900;
    nf::WindowMode window_mode = nf::WindowMode::Windowed;  // --fullscreen: Borderless
    bool vsync = true;
    float fov = 60.0f;
    int render_scale = 100;  // --render-scale
    int msaa = 1;            // --msaa
    nf::TextureFilter filter = nf::TextureFilter::Aniso8;  // --filter
    std::string language_pc = "en";  // --lang-pc
    bool can_save_settings = true;   // falso em capturas sem --config (não mexem no nightfire.ini)
    float sensitivity = nf::kDefaultMouseSensitivity;
    std::wstring screenshot;
    bool has_cam = false;
    float cam[6]{};
    bool draw_all = false;
    bool use = false;
    bool no_hd = false;
    bool no_lights = false;
    bool no_morphs = false;
    bool no_env_map = false;  // --no-env-map (depuração): as partes da rotina 0x2e0 com as UVs da malha
    bool no_fog = false;
    bool debug_shading = false;
    std::wstring textures;
    float wait = 0.0f;
    std::string input;  // entrada roteirizada (--input)
    bool has_at = false;
    float at[4]{};  // --at
    bool subtitles = true;  // --no-subtitles
    bool no_audio = false;  // --no-audio
    std::wstring audio_dump;  // --audio-dump
    float win = -1.0f;      // --win
    std::vector<int> channels;  // --channel
    std::vector<int> give_weapons;  // --weapon (a primeira vai para a mão)
    bool alpha_coverage = true;     // --no-alpha-coverage
    uint32_t shot_level = 0;  // --shot-level
    uint32_t shot_level_count = 1;  // --shot-level <hash>:<n>: a n-ésima carga desse pacote
    UiHdOptions hd_ui;      // --hd-ui, --hd-ui-dir, --hd-ui-anchor, --ui-inventory
    std::string prompts = "auto";  // --prompts, [ui] prompts
    uint32_t open_menu_page = 0;  // --open-menu-page
    bool unlock_all = false;      // --unlock-all
    bool debug_menu = false;      // --debug-menu
    std::string menu_tab;         // --menu-tab
    nf::MenuStyle menu_style = nf::MenuStyle::Modern;  // --menu-style, [ui] menu_style
    // O ritmo dos quadros (render/frame_interp.h): [video] fps_cap, interpolation, instant_mouse_look.
    int fps_cap = 0;                // --fps-cap (0: sem limite)
    bool interpolation = true;      // --no-interp
    bool instant_mouse = true;      // --no-instant-mouse
    int fixed_video_hz = 0;         // --fixed-video-hz: um monitor simulado (capturas determinísticas)
    float interp_alpha = -1.0f;     // --interp-alpha: depuração, o alpha fixo
    std::wstring frames_dump;       // --frames-dump <pasta>
    int frames = 0;                 // --frames <n>
    bool frames_png = true;         // --frames-no-png: só o frames.tsv
    bool only_2d = false;           // --only-2d: depuração, desenha só o 2D (o HUD, os menus) sobre preto
    // --renderer d3d9ff: o protótipo de Direct3D 9 de função fixa para o RTX Remix (render/remix_d3d9.h,
    // docs/remix.md); o padrão é d3d11.
    bool d3d9ff = false;
    bool d3d9_lights = true;        // --d3d9-no-lights: sem as luzes da VU1 como D3DLIGHT9
    std::wstring d3d9_remix_conf;   // --d3d9-remix-conf <arquivo>: um rtx.conf sugerido (hashes das texturas por uso)
    float d3d9_viewmodel = 0.0f;    // --d3d9-viewmodel <z>: a lista 2 no viewport de profundidade 0 a z (view model)
    // O adaptador de vídeo ([video] adapter, --adapter; platform/gpu_adapter.h) e o --list-adapters.
    std::string adapter = "auto";
    bool list_adapters = false;
    // --bench <s>: o benchmark (segundos de relógio medidos), --bench-out <arquivo>, --offscreen.
    float bench = 0.0f;
    std::wstring bench_out;
    std::wstring bench_frames;  // --bench-frames <tsv>
    bool bench_vsync = false;   // --bench-vsync
    bool offscreen = false;
    bool wait_given = false;  // --wait na linha de comando (no --bench o padrão é 1 s)
    std::wstring config_path;  // --config (o relatório do --bench diz de onde vieram as opções)
    // Os filmes (platform/psi_movie.h): [video] movies, --movies/--no-movies (as capturas e o --bench começam sem
    // eles, como antes), --movie-frame <n> (captura no quadro n de um filme; liga os filmes), --movie <hash> (só esse
    // filme conta para o --movie-frame), --intro-movie (com --level, a fase pedida com o filme de abertura).
    bool movies = true;
    int movies_flag = -1;     // --movies 1, --no-movies 0, sem nada -1
    // O decodificador de MPEG-2 dos filmes ([video] movie_decoder, --movie-decoder; platform/movie_decoder.h).
    nf::MovieDecoderChoice movie_decoder = nf::MovieDecoderChoice::Auto;
    int64_t movie_frame = -1;
    uint32_t movie_code = 0;
    bool intro_movie = false;
    // A tela de carga (platform/psi_loading.h): --load-screen <png> grava a primeira (com --load-screen-blob <n>, as
    // bolinhas da cadeia n por cima), --no-load-screen não a monta (depuração: as capturas de antes do porte dela).
    std::wstring load_screen;
    int load_screen_blob = -1;
    bool no_load_screen = false;
    // --no-env (depuração): DrawSnow = 0, InitDrops vazia e os posicionamentos do tempo e dos enfeites provisórios
    // (g_port_no_env), e sem a tela de carga: sem o vento das salas, a chuva, a neve, os raios, as nuvens, as
    // ondulações, as folhas, as árvores e as hélices, como antes do porte deles; para comparar capturas com as de antes
    // (todos eles e o GenLoadScreen sorteiam com o Rand).
    bool no_env = false;
    // --force-lightning (depuração): o céu "mostra o relâmpago com repique" em todo tick (SuperHappyLightningFlag, e
    // Reflicker a cada 10 ticks), para ver os raios (Lightning_Update) numa fase sem relâmpago no céu.
    bool force_lightning = false;
};

// Só do PC: a tela de carga (platform/psi_loading.h). O jogo carrega a fase dentro de um tick, sem desenhar; o gancho
// mostra na janela o que o PS2 mostraria: a tela preta do PS2WipeVram no começo da carga, a tela que o
// ResetMap_GenLoadScreen registrou (guardada numa cópia, porque as texturas dela somem no reinício seguinte) e, a cada
// passo das bolinhas, a cópia com os quadrados da cadeia em vigor (a textura delas guardada com uma referência
// própria). Nas capturas e no --bench fora da tela nada é apresentado; --load-screen grava a primeira tela num PNG.
struct LoadingPresenter {
    Renderer* renderer = nullptr;
    RuntimeGfx* gfx = nullptr;
    bool present = false;
    std::wstring shot;
    int shot_blob = -1;
    bool shot_done = false;
    bool snapshot = false;
    uint32_t presents = 0;
    uint32_t image = 0;  // a imagem da última tela (o sprite de fundo)
    GpuImageRef blob_image;  // a textura das bolinhas, com uma referência própria

    FrameParams params() const {
        FrameParams fp;
        fp.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        return fp;
    }
    void draw_blobs(int n) {
        Psi2DQuad q[kLoadingBlobCells];
        psi_loading_blob(n, q);
        std::vector<DrawItem> items;
        gfx->collect_2d(q, kLoadingBlobCells, items);
        for (DrawItem& d : items) {
            for (Quad2D& quad : d.quads2d) quad.image = blob_image.get();
        }
        renderer->render_2d(items, params());
    }
    void write_shot_report() {
        FILE* f = os::open_file(shot + L".txt", "w");
        if (f == nullptr) return;
        std::fprintf(f, "tela_carga fase %08x imagem %08x bolinhas textura %d %dx%d cadeia %d\n", GameState.load_file,
                     image, BlobTexture, BlobTextWidth, BlobTextHeight, shot_blob);
        for (const Psi2DQuad& q : g_psi_frame.quads2d) {
            if (q.origin.pass != Psi2DPass::Main) continue;
            if (q.origin.kind == Psi2DKind::Image) {
                std::fprintf(f, "imagem2d %.1f %.1f %.1f %.1f textura %08x\n", q.x0, q.y0, q.x1, q.y1, q.origin.texture_hash);
            }
        }
        const void* last = nullptr;
        for (const Psi2DQuad& q : g_psi_frame.quads2d) {
            if (q.origin.pass != Psi2DPass::Main || q.origin.kind != Psi2DKind::Glyph || q.origin.sprite == last) continue;
            last = q.origin.sprite;
            std::string text;
            for (const char* c = q.origin.text; c != nullptr && *c != 0; ++c) {
                if (uint8_t(*c) < 0x20 || uint8_t(*c) >= 0x7f) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\x%02x", uint8_t(*c));
                    text += buf;
                } else {
                    text += *c;
                }
            }
            std::fprintf(f, "texto2d %.1f %.1f fonte %u \"%s\"\n", q.x0, q.y0, q.origin.font, text.c_str());
        }
        std::fclose(f);
    }
    void on(PsiLoadingEvent e) {
        if (renderer == nullptr || gfx == nullptr) return;
        switch (e) {
            case PsiLoadingEvent::Wipe:
                // A VRAM zerada no começo de uma carga (não a do primeiro Game_Run).
                if (!present || GameFlow_GetState() != GF_LOAD_MAP) return;
                {
                    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                    renderer->clear_target(black);
                    renderer->present();
                    ++presents;
                }
                return;
            case PsiLoadingEvent::Screen: {
                if (!present && (shot.empty() || shot_done)) return;
                image = 0;
                for (const Psi2DQuad& q : g_psi_frame.quads2d) {
                    if (q.origin.kind == Psi2DKind::Image && image == 0) image = q.origin.texture_hash;
                }
                std::vector<DrawItem> items;
                gfx->collect_frame(nullptr, items);
                renderer->render(items, params());
                blob_image = renderer->keep_texture(BlobTexture);
                renderer->save_loading_snapshot();
                snapshot = true;
                if (!shot.empty() && !shot_done) {
                    if (shot_blob >= 0) draw_blobs(shot_blob);
                    renderer->save_png(shot);
                    write_shot_report();
                    shot_done = true;
                    if (shot_blob >= 0) renderer->restore_loading_snapshot();
                }
                if (present) {
                    renderer->present();
                    ++presents;
                }
                return;
            }
            case PsiLoadingEvent::Blob:
                if (!present || !snapshot || !renderer->restore_loading_snapshot()) return;
                draw_blobs(BlobToDraw);
                renderer->present();
                ++presents;
                return;
            case PsiLoadingEvent::End:
                renderer->release_loading_snapshot();
                blob_image.reset();
                snapshot = false;
                return;
        }
    }
};
LoadingPresenter g_loading;
void loading_hook(PsiLoadingEvent e) { g_loading.on(e); }

// --input: trechos de teclas (e do controle simulado) seguradas por um tempo de jogo.
struct InputStep {
    std::vector<int> keys;
    nf::PadState pad;
    bool uses_pad = false;
    bool pad_off = false;    // pad_off: o controle simulado desconectado neste trecho (o aviso do jogo)
    bool has_mouse = false;  // mouse=x,y: o cursor (tela do PS2) a partir deste trecho
    bool mouse_canvas = false;  // cmouse=x,y: na tela virtual de 1920 x 1080 do estilo Modern
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    float wheel = 0.0f;      // wheel=n: dentes no começo do trecho
    float look_dx = 0.0f;    // mlook=dx,dy: o mouse olhando, em contagens por segundo, durante o trecho
    float look_dy = 0.0f;
    float seconds = 0.0f;
};

// Um item mouse=x,y ou wheel=n de --input.
bool mouse_item(const std::string& item, InputStep* step) {
    if (item.compare(0, 6, "mouse=") == 0) {
        step->has_mouse = NF_SSCANF(item.c_str() + 6, "%f,%f", &step->mouse_x, &step->mouse_y) == 2;
        step->mouse_canvas = false;
        return step->has_mouse;
    }
    if (item.compare(0, 7, "cmouse=") == 0) {
        step->has_mouse = NF_SSCANF(item.c_str() + 7, "%f,%f", &step->mouse_x, &step->mouse_y) == 2;
        step->mouse_canvas = true;
        return step->has_mouse;
    }
    if (item.compare(0, 6, "wheel=") == 0) {
        step->wheel = std::stof(item.substr(6));
        return true;
    }
    if (item.compare(0, 6, "mlook=") == 0) {
        return NF_SSCANF(item.c_str() + 6, "%f,%f", &step->look_dx, &step->look_dy) == 2;
    }
    return false;
}

// Um item pad_* de --input no estado do controle simulado.
bool pad_item(const std::string& item, nf::PadState* pad) {
    if (item.compare(0, 4, "pad_") != 0) return false;
    const size_t eq = item.find('=');
    const std::string name = item.substr(0, eq);
    const float v = eq == std::string::npos ? 1.0f : std::clamp(std::stof(item.substr(eq + 1)), -1.0f, 1.0f);
    const int16_t axis = int16_t(v * 32767.0f);
    if (name == "pad_lx") pad->lx = axis;
    else if (name == "pad_ly") pad->ly = axis;
    else if (name == "pad_rx") pad->rx = axis;
    else if (name == "pad_ry") pad->ry = axis;
    else {
        nf::InputCode c;
        if (!nf::parse_input_name(name, &c) || !c.is_pad()) return false;
        if (c.code < 16) pad->buttons = uint16_t(pad->buttons | (1u << c.code));
        else if (c.code == nf::kPadInLT) pad->lt = uint8_t(std::fabs(v) * 255.0f);
        else if (c.code == nf::kPadInRT) pad->rt = uint8_t(std::fabs(v) * 255.0f);
        else return false;
    }
    return true;
}

int key_code(const std::string& name) {
    static const struct {
        const char* name;
        int vk;
    } named[] = {{"space", key::Space},     {"left", key::Left},     {"right", key::Right},   {"up", key::Up},
                 {"down", key::Down},       {"ctrl", key::Control},  {"lmb", key::LButton},   {"rmb", key::RButton},
                 {"enter", key::Return},    {"back", key::Back},     {"tab", key::Tab}};
    for (const auto& n : named) {
        if (name == n.name) return n.vk;
    }
    if (name.size() == 1) return std::toupper(uint8_t(name[0]));
    // Os nomes do nightfire.ini (platform/input_bindings.h).
    nf::InputCode c;
    return nf::parse_input_name(name, &c) && c.kind == nf::InputKind::Key ? c.code : 0;
}

std::vector<InputStep> parse_input(const std::string& text) {
    std::vector<InputStep> steps;
    size_t i = 0;
    while (i < text.size()) {
        const size_t end = std::min(text.find(' ', i), text.size());
        const std::string part = text.substr(i, end - i);
        i = end + 1;
        const size_t colon = part.find(':');
        if (part.empty() || colon == std::string::npos) continue;
        InputStep step;
        step.seconds = std::stof(part.substr(colon + 1));
        size_t k = 0;
        const std::string keys = part.substr(0, colon);
        while (k <= keys.size()) {
            const size_t plus = std::min(keys.find('+', k), keys.size());
            const std::string item = keys.substr(k, plus - k);
            if (item == "pad_off") {
                step.uses_pad = true;
                step.pad_off = true;
            } else if (pad_item(item, &step.pad)) {
                step.uses_pad = true;
            } else if (mouse_item(item, &step)) {
                // o mouse dos menus
            } else if (const int vk = key_code(item); vk != 0) {
                step.keys.push_back(vk);
            }
            k = plus + 1;
        }
        steps.push_back(step);
    }
    return steps;
}

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? char(c) : '?');
    return s;
}

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

bool file_exists(const std::wstring& p) { return os::exists(p); }

std::wstring from_utf8(const std::string& s) { return os::from_utf8(s); }

std::string to_utf8(const std::wstring& w) { return os::to_utf8(w); }

std::wstring exe_path() { return os::exe_path(); }

// O pacote de texturas HD do jogador (formato do PCSX2): ao lado do disco (a pasta dele, a da imagem .iso), senão na
// pasta do executável e nas de cima, senão na pasta atual. Com o disco numa pasta é a <pasta>\textures de antes.
std::wstring default_textures_dir(const nf::DiscSource& disc) {
    const std::wstring tail = os::join(L"", L"textures/SLUS-20579/replacements");
    std::vector<std::wstring> candidates{disc.folder()};
    std::wstring dir = exe_path();
    for (int up = 0; up < 4; ++up) {
        const size_t cut = dir.find_last_of(L"\\/");
        if (cut == std::wstring::npos) break;
        dir = dir.substr(0, cut);
        candidates.push_back(dir);
    }
    candidates.push_back(os::current_dir());
    for (const std::wstring& c : candidates) {
        std::wstring base = c;
        while (!base.empty() && (base.back() == L'\\' || base.back() == L'/')) base.pop_back();
        if (!base.empty() && file_exists(base + tail)) return base + tail;
    }
    std::wstring base = disc.folder();
    while (!base.empty() && (base.back() == L'\\' || base.back() == L'/')) base.pop_back();
    return base + tail;
}

// Só no PC: as opções salvas em nightfire.ini (platform/settings.h) são o ponto de partida; a linha de
// comando vale por cima. Com --screenshot o arquivo só é lido se --config for dado, para as capturas não
// dependerem das opções do jogador.
void apply_settings(Options& o, const nf::Settings& s) {
    // [video] e [ui] das opções de vídeo (platform/video_settings.h: as mesmas chaves que a página do menu grava).
    nf::VideoSettings v;
    v.width = o.width;
    v.height = o.height;
    v.window_mode = o.window_mode;
    v.vsync = o.vsync;
    v.render_scale = o.render_scale;
    v.msaa = o.msaa;
    v.filter = o.filter;
    v.fov = o.fov;
    v.hd_textures = !o.no_hd;
    v.hd_ui = o.hd_ui.enabled;
    v.language = o.language_pc;
    v.menu_style = o.menu_style;
    v.fps_cap = o.fps_cap;
    v.interpolation = o.interpolation;
    v.instant_mouse_look = o.instant_mouse;
    v.adapter = o.adapter;
    nf::load_video_settings(s, v);
    o.adapter = v.adapter;
    o.fps_cap = v.fps_cap;
    o.interpolation = v.interpolation;
    o.instant_mouse = v.instant_mouse_look;
    o.width = v.width;
    o.height = v.height;
    o.window_mode = v.window_mode;
    o.vsync = v.vsync;
    o.render_scale = v.render_scale;
    o.msaa = v.msaa;
    o.filter = v.filter;
    o.fov = v.fov;
    o.no_hd = !v.hd_textures;
    o.hd_ui.enabled = v.hd_ui;
    o.language_pc = v.language;
    o.menu_style = v.menu_style;
    o.hd_ui.anchor = s.get("ui", "anchor", o.hd_ui.anchor);
    o.prompts = s.get("ui", "prompts", o.prompts);
    o.subtitles = s.get_bool("ui", "subtitles", o.subtitles);
    o.movies = s.get_bool("video", "movies", o.movies);
    {
        const std::string dec = s.get("video", "movie_decoder", nf::movie_decoder_choice_name(o.movie_decoder));
        if (!nf::parse_movie_decoder_choice(dec, &o.movie_decoder)) {
            std::fprintf(stderr, "[video] movie_decoder = %s: use auto, mf ou own\n", dec.c_str());
        }
    }
    // O disco escolhido na tela de entrada (platform/disc_source.h; UTF-8 no arquivo) e se ela aparece a cada partida.
    o.disc_ini = from_utf8(s.get("game", "disc", ""));
    o.show_launcher = nf::ui::launcher_enabled(s);
    o.sensitivity = s.get_float("controls", "mouse_sensitivity", o.sensitivity);
    nf::load_controls(s, &nf::controls());  // [bindings] e o resto de [controls]
    // As opções do jogo que o .ini lembra (platform/game_options.h): psiInput_Init as aplica na partida.
    nf::load_game_options(s, &nf::game_options_ini());
    // As de jogabilidade só do PC: o estilo de mira e a câmera lenta da roda de armas (platform/pc_gameplay.h).
    nf::load_pc_gameplay(s, &nf::pc_gameplay());
}

// As opções de vídeo em Options (o nightfire.ini e a linha de comando), como a tela de entrada as mostra.
nf::VideoSettings video_settings_of(const Options& o) {
    nf::VideoSettings v;
    v.width = o.width;
    v.height = o.height;
    v.window_mode = o.window_mode;
    v.vsync = o.vsync;
    v.render_scale = o.render_scale;
    v.msaa = o.msaa;
    v.filter = o.filter;
    v.fov = o.fov;
    v.hd_textures = !o.no_hd;
    v.fps_cap = o.fps_cap;
    v.interpolation = o.interpolation;
    v.instant_mouse_look = o.instant_mouse;
    v.adapter = o.adapter;
    v.hd_ui = o.hd_ui.enabled;
    v.hud_edges = o.hd_ui.anchor == "edges";
    v.language = o.language_pc;
    v.menu_style = o.menu_style;
    return v;
}

Options parse_args(int argc, wchar_t** argv) {
    Options o;
    std::wstring config;
    bool capture = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--config" && i + 1 < argc) config = argv[++i];
        else if (a == L"--screenshot" || a == L"--bench") capture = true;  // o --bench também não mexe no ini
    }
    const bool loaded = nf::settings().load(config.empty() ? nf::default_settings_path() : config);
    if (loaded && (!capture || !config.empty())) apply_settings(o, nf::settings());
    // Os menus de controles gravam no arquivo; as capturas sem --config não mexem no do jogador.
    nf::g_controls_persist = !capture || !config.empty();
    o.can_save_settings = !capture || !config.empty();
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        const auto next = [&]() -> std::wstring { return i + 1 < argc ? argv[++i] : L""; };
        if (a == L"--data" || a == L"--disc") o.disc = next();
        else if (a == L"--setup" || a == L"--launcher") o.setup = true;
        else if (a == L"--setup-pick") o.setup_pick = next();
        else if (a == L"--setup-input") o.setup_input = narrow(next());
        else if (a == L"--pack-disc") o.pack_disc = next();
        else if (a == L"--out") o.pack_out = next();
        else if (a == L"--pack-no-movies") o.pack_movies = false;
        else if (a == L"--config") next();  // já lido acima
        else if (a == L"--level") o.level = narrow(next());
        else if (a == L"--save-dir") o.save_dir = next();
        else if (a == L"--width") o.width = std::stoi(next());
        else if (a == L"--height") o.height = std::stoi(next());
        else if (a == L"--fullscreen") o.window_mode = nf::WindowMode::Borderless;
        else if (a == L"--window-mode") nf::parse_window_mode(narrow(next()), &o.window_mode);
        else if (a == L"--render-scale") o.render_scale = std::clamp(std::stoi(next()), 25, 400);
        else if (a == L"--msaa") o.msaa = std::stoi(next());
        else if (a == L"--no-alpha-coverage") o.alpha_coverage = false;
        else if (a == L"--filter") nf::parse_texture_filter(narrow(next()), &o.filter);
        else if (a == L"--lang-pc") o.language_pc = narrow(next()) == "pt" ? "pt" : "en";
        else if (a == L"--novsync") o.vsync = false;
        else if (a == L"--fov") o.fov = std::stof(next());
        else if (a == L"--sensitivity") o.sensitivity = std::stof(next());
        else if (a == L"--screenshot") o.screenshot = next();
        else if (a == L"--drawall") o.draw_all = true;
        else if (a == L"--use") o.use = true;
        else if (a == L"--no-hd") o.no_hd = true;
        else if (a == L"--no-lights") o.no_lights = true;
        else if (a == L"--no-morphs") o.no_morphs = true;
        else if (a == L"--no-env-map") o.no_env_map = true;
        else if (a == L"--no-torso-aim") g_anim_aim_off = true;
        else if (a == L"--no-fog") o.no_fog = true;
        else if (a == L"--debug-shading") o.debug_shading = true;
        else if (a == L"--no-subtitles") o.subtitles = false;
        else if (a == L"--no-audio") o.no_audio = true;
        else if (a == L"--audio-dump") o.audio_dump = next();
        else if (a == L"--hd-ui") o.hd_ui.enabled = true;
        else if (a == L"--hd-ui-dir") o.hd_ui.dir = next();
        else if (a == L"--hd-ui-anchor") o.hd_ui.anchor = narrow(next());
        else if (a == L"--prompts") o.prompts = narrow(next());
        else if (a == L"--ui-inventory") o.hd_ui.inventory = next();
        else if (a == L"--textures") o.textures = next();
        else if (a == L"--wait") {
            o.wait = std::stof(next());
            o.wait_given = true;
        }
        else if (a == L"--win") o.win = std::stof(next());
        else if (a == L"--channel") o.channels.push_back(std::stoi(next(), nullptr, 16) & 0xff);
        else if (a == L"--weapon") {
            const std::wstring list = next();
            size_t p = 0;
            while (p <= list.size()) {
                const size_t c = std::min(list.find(L',', p), list.size());
                const std::wstring item = list.substr(p, c - p);
                if (item == L"all") {
                    for (int w = 1; w < 0x5f; ++w) o.give_weapons.push_back(-w);  // negativo: só se for da troca
                } else if (!item.empty()) {
                    o.give_weapons.push_back(std::stoi(item, nullptr, 16) & 0x7f);
                }
                p = c + 1;
            }
        }
        else if (a == L"--shot-level") {
            const std::string v = narrow(next());
            o.shot_level = uint32_t(std::stoul(v, nullptr, 16));
            if (const size_t colon = v.find(':'); colon != std::string::npos) {
                o.shot_level_count = uint32_t(std::max(1, std::stoi(v.substr(colon + 1))));
            }
        }
        else if (a == L"--input") o.input = narrow(next());
        else if (a == L"--open-menu-page") o.open_menu_page = uint32_t(std::stoul(narrow(next()), nullptr, 16));
        else if (a == L"--unlock-all") o.unlock_all = true;
        else if (a == L"--debug-menu") o.debug_menu = true;
        else if (a == L"--menu-tab") o.menu_tab = narrow(next());
        else if (a == L"--menu-style") nf::parse_menu_style(narrow(next()), &o.menu_style);
        else if (a == L"--fps-cap") nf::parse_fps_cap(narrow(next()), &o.fps_cap);
        else if (a == L"--no-interp") o.interpolation = false;
        else if (a == L"--interp") o.interpolation = true;
        else if (a == L"--no-instant-mouse") o.instant_mouse = false;
        else if (a == L"--aim-style") {
            AimStyle st;
            if (ads_parse_aim_style(narrow(next()).c_str(), &st)) nf::pc_gameplay().aim_style = uint8_t(st);
        } else if (a == L"--wheel-slowmo") nf::pc_gameplay().wheel_slowmo = std::clamp(std::stof(next()), 0.0f, 1.0f);
        else if (a == L"--fixed-video-hz") o.fixed_video_hz = std::clamp(std::stoi(next()), 1, 1000);
        else if (a == L"--interp-alpha") o.interp_alpha = std::clamp(std::stof(next()), 0.0f, 1.0f);
        else if (a == L"--frames-dump") o.frames_dump = next();
        else if (a == L"--frames") o.frames = std::max(0, std::stoi(next()));
        else if (a == L"--frames-no-png") o.frames_png = false;
        else if (a == L"--renderer") o.d3d9ff = narrow(next()) == "d3d9ff";
        else if (a == L"--d3d9-no-lights") o.d3d9_lights = false;
        else if (a == L"--d3d9-remix-conf") o.d3d9_remix_conf = next();
        else if (a == L"--d3d9-viewmodel") o.d3d9_viewmodel = std::clamp(std::stof(next()), 0.0f, 1.0f);
        else if (a == L"--only-2d") o.only_2d = true;
        else if (a == L"--adapter") {
            nf::AdapterPreference pref;
            if (nf::parse_adapter_preference(narrow(next()), &pref)) o.adapter = nf::adapter_preference_name(pref);
        }
        else if (a == L"--list-adapters") o.list_adapters = true;
        else if (a == L"--bench") o.bench = std::max(0.0f, std::stof(next()));
        else if (a == L"--bench-out") o.bench_out = next();
        else if (a == L"--bench-frames") o.bench_frames = next();
        else if (a == L"--bench-vsync") o.bench_vsync = true;
        else if (a == L"--offscreen") o.offscreen = true;
        else if (a == L"--movies") o.movies_flag = 1;
        else if (a == L"--no-movies") o.movies_flag = 0;
        else if (a == L"--movie-decoder") {
            const std::string v = narrow(next());
            if (!nf::parse_movie_decoder_choice(v, &o.movie_decoder)) {
                std::fprintf(stderr, "--movie-decoder %s: use auto, mf ou own\n", v.c_str());
            }
        }
        else if (a == L"--movie-frame") o.movie_frame = std::max<int64_t>(0, std::stoll(next()));
        else if (a == L"--movie") o.movie_code = uint32_t(std::stoul(narrow(next()), nullptr, 16));
        else if (a == L"--intro-movie") o.intro_movie = true;
        else if (a == L"--load-screen") o.load_screen = next();
        else if (a == L"--load-screen-blob") o.load_screen_blob = std::clamp(std::stoi(next()), 0, kLoadingBlobs - 1);
        else if (a == L"--no-load-screen") o.no_load_screen = true;
        else if (a == L"--no-env") o.no_env = true;
        else if (a == L"--force-lightning") o.force_lightning = true;
        else if (a == L"--at") {
            const std::string v = narrow(next());
            o.has_at = NF_SSCANF(v.c_str(), "%f,%f,%f,%f", &o.at[0], &o.at[1], &o.at[2], &o.at[3]) == 4;
        }
        else if (a == L"--cam") {
            const std::string v = narrow(next());
            o.has_cam = NF_SSCANF(v.c_str(), "%f,%f,%f,%f,%f,%f", &o.cam[0], &o.cam[1], &o.cam[2], &o.cam[3],
                                    &o.cam[4], &o.cam[5]) == 6;
        }
    }
    o.config_path = config;
    o.command_line = nf::ui::launcher_command_line(argc, argv);
    // O benchmark: sem limite de quadros nem VSync, e um segundo de aquecimento depois da carga se --wait não veio.
    if (o.bench > 0.0f) {
        if (!o.bench_vsync) {
            o.vsync = false;
            o.fps_cap = 0;
        }
        if (!o.wait_given) o.wait = 1.0f;
    }
    // O protótipo de Direct3D 9 só desenha o 2D original: os menus Modern e a camada 2D HD são Direct2D (Direct3D 11).
    if (o.d3d9ff) {
        o.menu_style = nf::MenuStyle::Original;
        o.hd_ui.enabled = false;
    }
    return o;
}

struct Camera {
    XMFLOAT3 pos{0, 0, 0};
    float yaw = 0, pitch = 0;
    float speed = 5.0f;

    XMVECTOR forward() const {
        return XMVectorSet(nf::fm::sin(yaw) * nf::fm::cos(pitch), nf::fm::sin(pitch), nf::fm::cos(yaw) * nf::fm::cos(pitch), 0);
    }
    void look_at(const float eye[3], const float target[3]) {
        pos = {eye[0], eye[1], eye[2]};
        const float dx = target[0] - eye[0], dy = target[1] - eye[1], dz = target[2] - eye[2];
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        yaw = nf::fm::atan2(dx, dz);
        pitch = len > 0 ? nf::fm::asin(dy / len) : 0;
        speed = std::max(2.0f, len * 0.25f);
    }
    // Onde o jogo poria o jogador (Player_Start -> Player_Init): na altura dos olhos, olhando para a
    // frente da rotação do ponto de partida (linha 2 de RotMatrix).
    void from_start(const _VECTOR& start, const _VECTOR& rot) {
        MATRIX m;
        RotMatrix(&rot, &m);
        const float eye[3] = {start.x, start.y + kEyeHeight, start.z};
        const float target[3] = {eye[0] + m.m[2][0], eye[1], eye[2] + m.m[2][2]};
        look_at(eye, target);
        speed = 5.0f;
    }
    // A partir de uma matriz de câmera do jogo (linha 2 para a frente, linha 3 a posição).
    void from_matrix(const MATRIX& m) {
        pos = {m.m[3][0], m.m[3][1], m.m[3][2]};
        yaw = nf::fm::atan2(m.m[2][0], m.m[2][2]);
        pitch = nf::fm::asin(std::clamp(m.m[2][1], -1.0f, 1.0f));
    }
    static constexpr float kEyeHeight = 1.6f;
};

// Matriz da câmera do jogo (viewer_tag::camera): linha 2 para a frente, linha 3 a posição e
// determinante +1 (é o que deixa os planos do frustum de Vision_Calculate_View_Fustrum virados para
// dentro).
void set_game_camera(viewer_tag* v, const Camera& cam, float fov_deg, float aspect) {
    XMFLOAT3 f, r, u;
    const XMVECTOR fwd = cam.forward();
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), fwd));
    XMStoreFloat3(&f, fwd);
    XMStoreFloat3(&r, right);
    XMStoreFloat3(&u, XMVector3Cross(fwd, right));
    v->camera = {{{r.x, r.y, r.z, 0}, {u.x, u.y, u.z, 0}, {f.x, f.y, f.z, 0}, {cam.pos.x, cam.pos.y, cam.pos.z, 1}}};
    // No PS2 Camera_CalcViewAngles fixa 60 graus e 4:3 ou 16:9; no PC valem os da janela.
    v->fov = XMConvertToRadians(fov_deg);
    v->aspect = aspect;
}

// Só no port, enquanto o jogador não existe: Espaço abre ou fecha a porta à frente da câmera, como
// se o jogador a usasse (Door_Activate com um objeto no lugar da câmera).
void use_door_in_front(const Camera& cam) {
    XMFLOAT3 f;
    XMStoreFloat3(&f, cam.forward());
    obj_tag* best = nullptr;
    float best_t = 4.0f;
    for (LLNODE_tag* n = TheDoors.first; n != nullptr; n = n->next) {
        obj_tag* door = reinterpret_cast<DOORINFO*>(n)->obj;
        const _VECTOR& s = door->sphere;
        const float dx = s.x - cam.pos.x, dy = s.y - cam.pos.y, dz = s.z - cam.pos.z;
        const float t = dx * f.x + dy * f.y + dz * f.z;  // distância ao longo do olhar
        const float px = dx - f.x * t, py = dy - f.y * t, pz = dz - f.z * t;
        const float off = std::sqrt(px * px + py * py + pz * pz);  // distância ao raio do olhar
        if (0.0f < t && t < best_t && off < s.w + 0.5f) {
            best = door;
            best_t = t;
        }
    }
    if (best == nullptr) return;
    alignas(16) static obj_tag stand_in;
    std::memset(&stand_in, 0, sizeof(stand_in));
    stand_in.sphere = {cam.pos.x, cam.pos.y, cam.pos.z, 0.5f};
    stand_in.pos = stand_in.sphere;
    stand_in.matrix.m[3][0] = cam.pos.x;
    stand_in.matrix.m[3][1] = cam.pos.y;
    stand_in.matrix.m[3][2] = cam.pos.z;
    Door_Activate(best, &stand_in, 0);
}

// Com --screenshot, um .txt ao lado da imagem descreve o quadro: a sala da câmera, as salas que a
// visibilidade deixou ver (índice na lista do mundo, tipo, se mostram o céu, profundidade de portais
// não é guardada) e quantos desenhos o jogo pediu. Serve para conferir a visibilidade.
void write_frame_report(const std::wstring& png, const std::string& level, size_t draws, const std::string& ui_hd,
                        const std::string& video) {
    FILE* f = os::open_file(png + L".txt", "w");
    if (f == nullptr) return;
    // Só do PC: as opções de vídeo em vigor (o renderizador) e a página de opções do menu.
    std::fprintf(f, "%s\n", video.c_str());
    {
        char pc[768];
        MenuPc_Describe(pc, sizeof(pc));
        std::fprintf(f, "%s\n", pc);
    }
    const auto index_of = [](const cel_tag* c) {
        int i = 0;
        for (const cel_tag* w = glb_world->first; w != nullptr; w = w->next, ++i) {
            if (w == c) return i;
        }
        return -1;
    };
    const viewer_tag* v = glb_viewer[0];
    std::fprintf(f, "ui_hd %s\n", ui_hd.c_str());
    std::fprintf(f, "fase %s\ncamera_sala %d ceu %d desenhos %zu\n", level.c_str(),
                 v != nullptr ? index_of(v->cel) : -1, v != nullptr ? v->sky_visible : 0, draws);
    // Os menus abertos (pausa, fim de missão, menu principal): página e foco de cada gerente; o mouse neles.
    char menus[512];
    Menu_Describe(menus, sizeof(menus));
    std::fprintf(f, "%s\n", menus);
    Menu_MouseDescribe(menus, sizeof(menus));
    std::fprintf(f, "%s\n", menus);
    // O fundo animado do painel da pausa (a textura 0x030000db, platform/psi_woman.h): o último quadro decodificado.
    psi_woman_describe(menus, sizeof(menus));
    std::fprintf(f, "%s\n", menus);
    // O menu principal (o perfil, a operação do cartão) e o cartão de memória.
    Menu_FrontEndDescribe(menus, sizeof(menus));
    std::fprintf(f, "%s\n", menus);
    // O dossiê (a página aberta) e as trapaças da página de depuração.
    MenuDossier_Describe(menus, sizeof(menus));
    std::fprintf(f, "%s\n", menus);
    MenuCheats_Describe(menus, sizeof(menus));
    std::fprintf(f, "%s\n", menus);
    psi_card_describe(menus, sizeof(menus));
    std::fprintf(f, "%s\n", menus);
    // As estatísticas da missão (o jogador 0) e a pontuação guardada da missão em BaseMap.
    PlrStat_Describe(menus, sizeof(menus));
    std::fprintf(f, "estatisticas %s\n", menus);
    // NF_MENU_DUMP=1: os controles da página atual de cada gerente (id, tag, tipo, estado, retângulo e textos),
    // para conferir as páginas do disco.
    if (std::getenv("NF_MENU_DUMP") != nullptr) {
        for (int i = 0; i < kMenuManagers; ++i) {
            const M_MANAGER& m = menu_manager[i];
            if (m.active == 0 || m.page == nullptr) continue;
            for (const M_CONTROL* c = static_cast<const M_CONTROL*>(ll_first(m.page->controls)); c != nullptr; c = c->next) {
                std::fprintf(f, "controle %08x tag %u tipo %u estado %u %d %d %d %d", c->id, c->tag, c->type, c->state, c->x,
                             c->y, c->w, c->h);
                if (c->type == MCT_LABEL) {
                    const M_LABEL* l = reinterpret_cast<const M_LABEL*>(c);
                    std::fprintf(f, " rotulo %08x \"%s\"", l->label, l->string != nullptr ? nf::raw(l->string) : "");
                } else if (c->type == MCT_BUTTON) {
                    const M_BUTTON* b = reinterpret_cast<const M_BUTTON*>(c);
                    std::fprintf(f, " rotulo %08x \"%s\"", b->label.label, b->label.string != nullptr ? nf::raw(b->label.string) : "");
                } else if (c->type == MCT_LIST) {
                    const M_LISTBOX* l = reinterpret_cast<const M_LISTBOX*>(c);
                    std::fprintf(f, " linhas %u escolhida %d", l->rows.count,
                                 l->selected != nullptr ? l->selected->data : -1);
                    for (const LIST_ROW* r = static_cast<const LIST_ROW*>(ll_first(l->rows)); r != nullptr; r = r->next) {
                        std::fprintf(f, " [%d:", r->data);
                        for (int k = 0; k < l->columns && k < 5; ++k) std::fprintf(f, "%s\"%s\"", k ? "|" : "", r->cells[k].text);
                        std::fprintf(f, "]");
                    }
                } else if (c->type == MCT_MEMO) {
                    const M_MEMOBOX* mm = reinterpret_cast<const M_MEMOBOX*>(c);
                    std::fprintf(f, " \"%s\"", mm->text != nullptr ? nf::raw(mm->text) : "");
                } else if (c->type == MCT_RADIO) {
                    const M_RADIO* r = reinterpret_cast<const M_RADIO*>(c);
                    std::fprintf(f, " opcoes %u valor %d \"%s\"", r->options.count,
                                 r->selected != nullptr ? r->selected->data : -1,
                                 r->selected != nullptr ? r->selected->cells[0].text : "");
                } else if (c->type == MCT_SCROLL) {
                    const M_SCROLL* s = reinterpret_cast<const M_SCROLL*>(c);
                    std::fprintf(f, " barra %d..%d pos %d", s->min, s->max, s->pos);
                }
                std::fprintf(f, "\n");
            }
        }
    }
    // A entrada do jogador 0: o aparelho dos analógicos (0 teclado e mouse, 1 controle), o controle do
    // XInput, as opções do jogo, as teclas diferentes do padrão e a última mensagem da página de teclas.
    {
        const nf::Bindings defaults = nf::default_bindings();
        std::string changed;
        for (int i = 0; i < nf::kBindCommandCount; ++i) {
            const std::string now = nf::format_inputs(nf::controls().bindings.in[i], nf::kMaxBinds);
            if (now != nf::format_inputs(defaults.in[i], nf::kMaxBinds)) {
                changed += std::string(" ") + nf::kBindCommands[i].key + "=" + (now.empty() ? "-" : now);
            }
        }
        std::fprintf(f, "entrada aparelho %d controle %d estilo %u inverte %u vibra %u teclas%s | %s\n",
                     psiInput_ActiveDevice(), psiInput_PadIndex(), PlayerSetting[0].style, PlayerSetting[0].invert_look,
                     PlayerSetting[0].unknown_09, changed.empty() ? " padrao" : changed.c_str(), MenuPcControls_Status());
        // As opções do jogo nos lugares delas (* = o .ini a lembra; platform/game_options.h).
        char line[512];
        nf::game_options_describe(line, sizeof(line));
        std::fprintf(f, "%s\n", line);
    }
    {
        // O som: itens vivos no IOP, vozes da SPU com envelope, falas tocando, o banco, os pedidos do jogo e o que
        // saiu nos últimos 0.5 s (dBFS).
        const nf::SfxIopState st = nf::sfx_iop_state();
        const nf::AudioOutStats out = nf::audio_out_stats();
        const char* mode = nf::audio_out_mode() == nf::AudioMode::Device ? "aparelho"
                           : nf::audio_out_mode() == nf::AudioMode::Dump ? "wav"
                                                                           : "mudo";
        std::fprintf(f,
                     "audio %s vozes %d tocando %d falas %d banco SB_%d reverb %d pausa %d cena %d | jogo pedidos %u "
                     "tocados %u falas %u nos %d | iop pedidos %u comecados %u recusados %u ausentes %u roubados %u | "
                     "saida pico %.1f rms %.1f frames %llu\n",
                     mode, st.voices, st.items, st.speech_playing, st.bank_slot, st.reverb, st.paused, st.nis_playing,
                     g_sound_stats.requested, g_sound_stats.played, g_sound_stats.speech, sound_nodes_in_use(),
                     nf::g_sfx_iop_stats.requested, nf::g_sfx_iop_stats.started, nf::g_sfx_iop_stats.rejected,
                     nf::g_sfx_iop_stats.not_found, nf::g_sfx_iop_stats.stolen, out.peak_db, out.rms_db,
                     static_cast<unsigned long long>(out.frames));
        // A música do EE (engine/music.cpp): a linha de MapMusic, a faixa, o trecho que toca, o último pedido, o
        // ataque e o fim.
        const MusicState ms = music_state();
        std::fprintf(f, "musica fase %08x faixa 0x%x trecho %d pedido %d ataque %d fim %d\n", uint32_t(ms.level),
                     uint32_t(ms.track), ms.last_jump, ms.request, ms.under_attack, ms.wait_for_end);
    }
    if (const obj_tag* p = glb_players[0]; p != nullptr) {
        std::fprintf(f, "jogador %.3f %.3f %.3f giro %.3f subestado %u\n", p->pos.x, p->pos.y, p->pos.z, p->rot.y,
                     p->unknown_f6);
        const BLData* b = static_cast<const BLData*>(p->data);
        std::fprintf(f, "olhar %.3f\n", b->pitch);  // BLData::pitch (x 90 graus)
        // A água: o nível da sala do jogador (-500 sem água), os olhos, submerso (water_flags 0x10), o ar e a fase da
        // ondulação da câmera debaixo d'água (Camera_Shear, viewer_tag +0x1e0) com o borrão da tela.
        {
            float phase = 0.0f;
            const viewer_tag* pv = glb_viewer[uint8_t(b->player) % kViewerCount];
            if (pv != nullptr) std::memcpy(&phase, &pv->unknown_1e0[0], 4);
            _VECTOR head{};
            AnimGetBoneWorldTrans(const_cast<obj_tag*>(p), 0x80000005u, 0, &head, nullptr);
            std::fprintf(f, "agua nivel %.2f olhos %.2f cabeca %.2f submerso %u ar %.1f ondulacao %.3f borrao %u\n",
                         p->cel != nullptr ? p->cel->water_y : -500.0f,
                         pv != nullptr ? pv->camera.m[3][1] : 0.0f, head.y, (b->water_flags & 0x10) != 0 ? 1u : 0u,
                         b->air, phase, GlobalBlur);
        }
        std::fprintf(f, "vida %.1f colete %.1f vermelho %u morto %d missao_falhou %d recomecos %u\n", b->health,
                     b->armour, b->pain_flash, p->type == 0x12 ? 1 : 0, switch_channels[0x62] != 0 ? 1 : 0,
                     g_mission_port.restarts);
        // O que as panes do HUD mostram e o estado da missão (o desenho do HUD é do módulo 2D).
        char line[512];
        HUD_Describe(b, line, sizeof(line));
        std::fprintf(f, "hud %s\n", line);
        Mission_Describe(line, sizeof(line));
        std::fprintf(f, "missao %s recomecos %u\n", line, g_mission_port.restarts);
        const ANIMOBJ_tag* anim = reinterpret_cast<const ANIMOBJ_tag*>(nf::raw(p->sphere_src));
        if (b->gun != nullptr && anim != nullptr) {
            const int8_t w = anim->weapon;
            std::fprintf(f, "arma %d estado %u pente %d municao %d mirando %d zoom %.2f\n", w, b->gun->state,
                         b->weapons[w].rounds, b->ammo[weapon_def(w)->ammo_type], anim->flags & 1, b->zoom);
            // A mira moderna (engine/player_ads.h): o estilo, o aparelho, o peso da pose e o zoom leve; a pose da arma.
            {
                char ads_line[256];
                ads_describe(ads_line, sizeof(ads_line));
                std::fprintf(f, "%s arma_pos %.3f %.3f %.3f arma_giro %.3f %.3f %.3f", ads_line, b->gun->pos.x,
                             b->gun->pos.y, b->gun->pos.z, b->gun->rot.x, b->gun->rot.y, b->gun->rot.z);
                // A alça e a massa da tabela levadas pela matriz da arma até o espaço da câmera 0 (lado, cima, frente).
                const ANIMOBJ_tag* ga = reinterpret_cast<const ANIMOBJ_tag*>(nf::raw(b->gun->sphere_src));
                const AdsSightEntry* se = ga != nullptr ? ads_sight_entry(ga->skin->hash) : nullptr;
                if (se != nullptr && glb_viewer[0] != nullptr) {
                    const MATRIX& gm = b->gun->matrix;
                    const MATRIX& cm = glb_viewer[0]->camera;
                    for (const float* p : {se->rear, se->front}) {
                        float w[3], v[3];
                        for (int k = 0; k < 3; ++k) w[k] = p[0] * gm.m[0][k] + p[1] * gm.m[1][k] + p[2] * gm.m[2][k] + gm.m[3][k];
                        for (int r = 0; r < 3; ++r) {
                            v[r] = (w[0] - cm.m[3][0]) * cm.m[r][0] + (w[1] - cm.m[3][1]) * cm.m[r][1] +
                                   (w[2] - cm.m[3][2]) * cm.m[r][2];
                        }
                        std::fprintf(f, " %s %.3f %.3f %.3f", p == se->rear ? "alca" : "massa", v[0], v[1], v[2]);
                    }
                }
                std::fprintf(f, "\n");
            }
            // A mira automática (Check_AutoAim): a opção (PlayerSetting::autoaim), o alvo (tipo e distância) e a mira
            // suavizada na tela (aim_cur_x/y, que a bala e a arma seguem).
            {
                const auto* t = static_cast<const obj_tag*>(b->aim_target);
                std::fprintf(f, "mira_auto ligada %u alvo %x dist %.2f tela %.3f %.3f\n",
                             PlayerSetting[uint8_t(b->player) & 3].autoaim, t != nullptr ? t->type : 0u,
                             t != nullptr ? Vec_Dist3D(&t->pos, &p->pos) : 0.0f, b->aim_cur_x, b->aim_cur_y);
            }
            // O laser da arma (Player_LaserPointer, Draw_LaserBeam, Draw_TaserBeam): o feixe (BLData::fx[0]: modelo,
            // alfa, comprimento, câmeras que o desenham), o ponto (BLData::glow) e os ponteiros do relógio.
            const obj_tag* beam = b->fx[0];
            const obj_tag* dot = b->glow;
            if (beam != nullptr && dot != nullptr) {
                int segments = 0;
                for (const obj_tag* o : b->fx) segments += o != nullptr && o->unknown_f8 != 0 && o->glist != nullptr;
                // Quantos desenhos do quadro são o feixe e o ponto (pelo modelo).
                int drawn_beam = 0, drawn_dot = 0;
                for (const PsiDrawCall& c : g_psi_frame.draws) {
                    drawn_beam += beam->glist != nullptr && c.glist == beam->glist;
                    drawn_dot += dot->glist != nullptr && c.glist == dot->glist;
                }
                std::fprintf(f,
                             "laser ligado %u feixe %08x alfa %u comprimento %.2f vistas %x segmentos %d ponto %.2f %.2f "
                             "%.2f vistas %x escala %.2f relogio %d desenhados %d/%d\n",
                             b->gun_flag8, beam->glist != nullptr ? hashtable_celglist_to_hashcode(beam->glist) : 0u,
                             beam->unknown_103[3], beam->scale, beam->unknown_f8, segments, dot->pos.x, dot->pos.y,
                             dot->pos.z, dot->unknown_f8, dot->scale,
                             (Hours != nullptr) + (Minutes != nullptr) + (Seconds != nullptr), drawn_beam, drawn_dot);
            }
        }
        int by_type[256] = {};
        for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) ++by_type[o->type];
        std::fprintf(f, "efeitos marcas %u fumaca %d detritos %d rastros %d balas %d\n", EffectList.used.count,
                     by_type[0xe], by_type[0x10], by_type[0x41], by_type[5]);
        // Itens pegáveis à vista (tipo 0x2f, sem a flag 0x10) e o mais perto do jogador.
        int items = 0;
        const obj_tag* nearest = nullptr;
        for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
            if (o->type != kPickupObjectType || (o->life & 1) != 0 || (o->flags & 0x10) != 0) continue;
            ++items;
            if (nearest == nullptr || Vec_SqDist3D(&o->sphere, &p->pos) < Vec_SqDist3D(&nearest->sphere, &p->pos)) {
                nearest = o;
            }
        }
        std::fprintf(f, "itens %d", items);
        if (nearest != nullptr) {
            const PICKUPINFO* info = static_cast<const PICKUPINFO*>(nearest->data);
            std::fprintf(f, " perto %.3f %.3f %.3f dist %.2f tipo %u item %u estado %u", nearest->sphere.x,
                         nearest->sphere.y, nearest->sphere.z, Vec_Dist3D(&nearest->sphere, &p->pos), info->kind,
                         info->item, info->state);
        }
        std::fprintf(f, "\n");
    }
    // Emissores vivos e as listas de partículas (psiDrawParticleList) do último Game_Draw.
    int emitters = 0;
    for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) emitters += o->type == kEmitterObjectType;
    std::fprintf(f, "emissores %d listas %zu particulas %zu\n", emitters, g_psi_frame.particle_lists.size(),
                 g_psi_frame.particles.size());
    // Destruição: quebráveis vivos (0x20) e quebrados, estilhaços vivos/criados e triângulos de
    // estilhaço no último quadro, explosões criadas/vivas (0xf), destrutíveis (0x21) e quebrados, zonas
    // que machucam (0x27).
    {
        int by_type[256] = {};
        for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
            if ((o->life & 1) == 0) ++by_type[o->type];
        }
        std::fprintf(f, "destruicao quebraveis %d quebrados %u cacos %u/%u triangulos %zu explosoes %u/%d destrutiveis %d "
                        "quebrados %u zonas %d\n",
                     by_type[0x20], g_break_stats.broken, ShardList.count, g_break_stats.shards,
                     g_psi_frame.tri_vertices.size() / 3, g_explode_stats.created, by_type[0xf], by_type[0x21],
                     g_break_stats.smashed, by_type[0x27]);
    }
    // Corpos rígidos (engine/rigid_body.h): quantos por tipo e, do mais perto da câmera, a posição, quanto o "cima" dele
    // se inclinou da vertical, a velocidade angular e a linear; e os contadores desde a partida.
    {
        int count = 0;
        int kinds[5] = {};
        const obj_tag* near_rb = nullptr;
        float near_d = 1e30f;
        const viewer_tag* cam = glb_viewer[0];
        for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
            if (o->type != kRigidBodyType || (o->life & 1) != 0) continue;
            ++count;
            const auto* rbd = static_cast<const RIGIDBODY*>(o->data);
            ++kinds[std::min<uint16_t>(rbd->kind, 4)];
            if (std::getenv("NF_LIST_RB") != nullptr) {
                std::fprintf(f, "corpo %.2f %.2f %.2f tipo %u massa %.1f raio %.2f modelo %08x colisao %d\n",
                             o->matrix.m[3][0], o->matrix.m[3][1], o->matrix.m[3][2], rbd->kind, rbd->mass,
                             rbd->test.radius, o->glist != nullptr ? hashtable_celglist_to_hashcode(o->glist) : 0u,
                             o->glist != nullptr && o->glist->coll != nullptr ? 1 : 0);
            }
            if (cam == nullptr) continue;
            const float dx = o->matrix.m[3][0] - cam->camera.m[3][0], dy = o->matrix.m[3][1] - cam->camera.m[3][1];
            const float dz = o->matrix.m[3][2] - cam->camera.m[3][2];
            if (dx * dx + dy * dy + dz * dz < near_d) {
                near_d = dx * dx + dy * dy + dz * dz;
                near_rb = o;
            }
        }
        std::fprintf(f, "corpos_rigidos %d tipos %d,%d,%d,%d,%d criados %u tiros %u batidas %u carregados %u capsula %u",
                     count, kinds[0], kinds[1], kinds[2], kinds[3], kinds[4], g_rigid_body_stats.created,
                     g_rigid_body_stats.bullet_hits, g_rigid_body_stats.bounces, g_rigid_body_stats.carried,
                     g_rigid_body_stats.cylinder_hits);
        if (near_rb != nullptr) {
            const auto* rb = static_cast<const RIGIDBODY*>(near_rb->data);
            const float tilt = nf::fm::acos(std::max(-1.0f, std::min(1.0f, near_rb->matrix.m[1][1]))) * 57.29578f;
            std::fprintf(f, " | perto %.2f %.2f %.2f dist %.2f tipo %u massa %.1f inclinacao %.2f giro %.3f vel %.3f",
                         near_rb->matrix.m[3][0], near_rb->matrix.m[3][1], near_rb->matrix.m[3][2], std::sqrt(near_d),
                         rb->kind, rb->mass, tilt, Vec_Magnitude(&rb->omega), Vec_Magnitude(&rb->vel));
        }
        std::fprintf(f, "\n");
    }
    // Segurança (câmeras, monitores, holofotes, minas, o canhão montado, os mísseis): engine/sensor.h.
    {
        char line[512];
        Security_Describe(line, sizeof(line));
        std::fprintf(f, "%s\n", line);
    }
    // Iluminação da VU1 e névoa: desenhos de modelo sem luz, com 1 e 2 luzes, com tweak, e onde a
    // passada de névoa entra (-1: sem névoa).
    size_t lit[3] = {}, tweaked = 0;
    for (const PsiDrawCall& c : g_psi_frame.draws) {
        if (c.glist == nullptr) continue;
        ++lit[std::min<size_t>(c.num_lights, 2)];
        tweaked += (c.tweak[0] & c.tweak[1] & c.tweak[2] & c.tweak[3]) != 0xff;
    }
    // As limpezas do Z (psiClearZ) do Game_Draw: o desenho antes do qual cada uma caiu (a de psiPreDraw é 0; a da
    // lista 2 vem depois dos transparentes) e quantos desenhos da lista 2 vêm depois dela.
    std::string clears;
    for (const int32_t at : g_psi_frame.clear_z_at) clears += (clears.empty() ? "" : ",") + std::to_string(at);
    size_t list2 = 0;
    for (const PsiDrawCall& c : g_psi_frame.draws) list2 += c.list == 2;
    std::fprintf(f, "luzes sem %zu uma %zu duas %zu tweak %zu nevoa %d limpa_z %s lista2 %zu\n", lit[0], lit[1], lit[2],
                 tweaked, g_psi_frame.fog_at, clears.empty() ? "-" : clears.c_str(), list2);
    // Os esmaecimentos (psiFadeView) do quadro: quantos, e de cada um a câmera, a cor e o alfa (escala do GS) e o
    // estado do Camera_SetFade dela (fade_state, o tempo 0..1 e a velocidade por tick).
    {
        std::string fades;
        for (const PsiDrawCall& c : g_psi_frame.draws) {
            if (c.fade == 0) continue;
            int cam = -1;
            for (int i = 0; i < kViewerCount; ++i) {
                if (glb_viewer[i] != nullptr && glb_viewer[i] == c.camera) cam = i;
            }
            char one[160];
            const viewer_tag* v = cam >= 0 ? glb_viewer[cam] : nullptr;
            std::snprintf(one, sizeof(one), " | camera %d cor %02x%02x%02x alfa %u estado %u tempo %.3f passo %.4f", cam,
                          c.fade_rgba[0], c.fade_rgba[1], c.fade_rgba[2], unsigned(c.fade_rgba[3]),
                          v != nullptr ? unsigned(v->fade_state) : 0u, v != nullptr ? double(v->fade) : 0.0,
                          v != nullptr ? double(v->fade_speed) : 0.0);
            fades += one;
        }
        std::fprintf(f, "esmaecimento %u%s\n", g_psi_frame.fades, fades.c_str());
        // O clarão (Player_SetFlashBang: a granada de luz e o tremor) do jogador 0 e os tremores da fase (canal,
        // segundos, clarão, ligado).
        std::string quakes;
        for (const obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
            if (o->type != 0x54) continue;
            const auto* a = static_cast<const APOCALYPSE_tag*>(o->data);
            char one[64];
            std::snprintf(one, sizeof(one), " %x:%.0f:%u:%u", unsigned(a->channel), double(a->total / 60.0f),
                          unsigned(a->flash), unsigned(a->on));
            quakes += one;
        }
        const BLData* fb = glb_players[0] != nullptr ? bloke_of(glb_players[0]) : nullptr;
        // rastro: o ScreenBlur do último Game_Draw (0x80 = sem rastro; 0x5c debaixo d'água, GlobalBlur 0x91).
        std::fprintf(f, "clarao borrao %u resta %.1f total %.1f rastro %u | tremores%s\n",
                     fb != nullptr ? unsigned(fb->blur) : 0u, fb != nullptr ? double(fb->fade_left) : 0.0,
                     fb != nullptr ? double(fb->fade_time) : 0.0, unsigned(g_psi_frame.screen_blur),
                     quakes.empty() ? " -" : quakes.c_str());
    }
    // A tela de carga (platform/psi_loading.h): quantas, as limpezas da VRAM, as bolinhas montadas, os passos delas, as
    // chamadas da bomba, quanto tempo a última ficou (do psiStartLoadingScreen ao psiEndLoadingScreen) e a textura das
    // bolinhas.
    // O tempo (engine/weather.h) e os enfeites (engine/scenery.h).
    {
        char line[512];
        Weather_Describe(line, sizeof(line));
        std::fprintf(f, "%s\n", line);
        Scenery_Describe(line, sizeof(line));
        std::fprintf(f, "%s\n", line);
    }
    std::fprintf(f, "tela_carga telas %u limpezas %u bolinhas %u passos %u bombas %u ms %.1f textura %d %dx%d imagem %08x "
                    "apresentadas %u\n",
                 g_psi_loading_stats.screens, g_psi_loading_stats.wipes, g_psi_loading_stats.blobs,
                 g_psi_loading_stats.steps, g_psi_loading_stats.pumps, g_psi_loading_stats.last_ms, BlobTexture,
                 BlobTextWidth, BlobTextHeight, g_loading.image, g_loading.presents);
#if NF_PTR64
    // 64 bits: a arena (platform/arena.h), onde ficam o heap do jogo, todo new/delete e a pilha do jogo.
    {
        const nf::arena::Stats a = nf::arena::stats();
        std::fprintf(f, "arena comprometida %.1f MB em_uso %.1f MB pico %.1f MB alocacoes %llu pedacos %u/%u\n",
                     double(a.committed) / 1048576.0, double(a.in_use) / 1048576.0, double(a.peak) / 1048576.0,
                     static_cast<unsigned long long>(a.allocations), a.chunks_used, a.chunks_total);
    }
#endif
    // Personagens da IA: quantos e o mais perto da câmera (posição, giro, estado, DAnim, pele).
    {
        const Drone_tag* best = nullptr;
        float best_d = 1e30f;
        int count = 0;
        const viewer_tag* cam = glb_viewer[0];
        for (const LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
            const Drone_tag* d = reinterpret_cast<const Drone_tag*>(n);
            ++count;
            if (cam == nullptr) continue;
            const float dx = d->obj->pos.x - cam->camera.m[3][0], dy = d->obj->pos.y - cam->camera.m[3][1];
            const float dz = d->obj->pos.z - cam->camera.m[3][2];
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < best_d) {
                best_d = dist;
                best = d;
            }
        }
        std::fprintf(f, "canais");
        for (int ch = 1; ch < 256; ++ch) {
            if (switch_channels[ch] != 0) std::fprintf(f, " %x", ch);
        }
        std::fprintf(f, "\n");
        // Gatilhos (tipo 0x1d): quantos, quantos com geometria de colisão e quantos estão sendo tocados;
        // e as flags do mais perto.
        int triggers = 0, with_coll = 0, touched = 0;
        const obj_tag* near_trigger = nullptr;
        float near_d = 1e30f;
        for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
            if (o->type != 0x1d) continue;
            ++triggers;
            if (o->glist != nullptr && o->glist->coll != nullptr) ++with_coll;
            if (o->hits != nullptr) ++touched;
            if (std::getenv("NF_LIST_TRIGGERS") != nullptr) {
                const TRIGGER_Tag* t = static_cast<const TRIGGER_Tag*>(o->data);
                std::fprintf(f, "gatilho %.2f %.2f %.2f modo %u canal %x toque %x coll %d\n", o->pos.x, o->pos.y, o->pos.z,
                             t->mode, t->channel, t->touch, o->glist != nullptr && o->glist->coll != nullptr ? 1 : 0);
            }
            if (cam != nullptr) {
                const float dx = o->pos.x - cam->camera.m[3][0], dz = o->pos.z - cam->camera.m[3][2];
                if (dx * dx + dz * dz < near_d) {
                    near_d = dx * dx + dz * dz;
                    near_trigger = o;
                }
            }
        }
        std::fprintf(f, "gatilhos %d com_colisao %d tocados %d", triggers, with_coll, touched);
        if (near_trigger != nullptr) {
            std::fprintf(f, " perto %.1f m flags %08x mode %x", std::sqrt(near_d), near_trigger->flags,
                         near_trigger->mode);
        }
        std::fprintf(f, "\n");
        std::fprintf(f, "inimigos %d", count);
        if (best != nullptr) {
            const obj_tag* o = best->obj;
            std::fprintf(f, " perto %.2f %.2f %.2f giro %.3f dist %.2f estado %u danim %u pele %08x", o->pos.x, o->pos.y,
                         o->pos.z, o->rot.y, best_d, best->sm.state, best->danim, best->skin);
        }
        std::fprintf(f, "\n");
        // A mira do tronco (AnimObjectAimAt): quantos personagens miram (anim_flags 0x180), quantos com o alvo no
        // alcance (0x1000) e, do mais perto que mira, o giro e a inclinação da coluna e o ângulo entre o cano
        // (DroneWeap_GetWeaponInfo) e target_pos, em graus.
        {
            int aiming = 0, in_range = 0;
            Drone_tag* near_aim = nullptr;
            float near_aim_d = 1e30f;
            for (const LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
                Drone_tag* d = const_cast<Drone_tag*>(reinterpret_cast<const Drone_tag*>(n));
                const ANIMOBJ_tag* a = d->obj != nullptr ? anim_of(d->obj) : nullptr;
                if (a == nullptr || (a->anim_flags & 0x180) == 0) continue;
                ++aiming;
                in_range += (a->anim_flags & 0x1000) != 0;
                if (cam == nullptr) continue;
                const float dx = d->obj->pos.x - cam->camera.m[3][0], dz = d->obj->pos.z - cam->camera.m[3][2];
                if (dx * dx + dz * dz < near_aim_d) {
                    near_aim_d = dx * dx + dz * dz;
                    near_aim = d;
                }
            }
            std::fprintf(f, "mira_ia mirando %d no_alcance %d guardas %u", aiming, in_range, g_anim_aim_guards);
            if (near_aim != nullptr) {
                const ANIMOBJ_tag* a = anim_of(near_aim->obj);
                int32_t bone = 0;
                uint32_t flash = 0;
                uint16_t kind = 0;
                _VECTOR pos{}, dir{};
                float err = -1.0f;
                if (DroneWeap_GetWeaponInfo(near_aim, &bone, &flash, &kind, &pos, &dir) != 0) {
                    const float tx = near_aim->target_pos.x - pos.x, ty = near_aim->target_pos.y - pos.y;
                    const float tz = near_aim->target_pos.z - pos.z;
                    const float lt = std::sqrt(tx * tx + ty * ty + tz * tz);
                    const float ld = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                    if (lt > 0.0f && ld > 0.0f) {
                        const float c = (tx * dir.x + ty * dir.y + tz * dir.z) / (lt * ld);
                        err = nf::fm::acos(std::max(-1.0f, std::min(1.0f, c))) * 57.29578f;
                    }
                }
                std::fprintf(f, " | perto %.2f %.2f %.2f giro_tronco %.3f inclinacao %.3f flags %x erro_cano %.1f",
                             near_aim->obj->pos.x, near_aim->obj->pos.y, near_aim->obj->pos.z, a->aim_yaw, a->aim_pitch,
                             a->anim_flags & 0x7380u, err);
            }
            std::fprintf(f, "\n");
        }
        // Geradores de personagens (objetos 0x30 com grupo, DroneSpawner_*): os que ainda estão no mapa, os
        // modelos que tiraram do mapa, os personagens que geraram e as levas fechadas desde a partida, e os
        // gerados vivos nas vagas.
        int spawners = 0, spawned_alive = 0;
        for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
            const auto* s = static_cast<const DroneSpawner_tag*>(o->data);
            if (o->type != 0x30 || (o->life & 1) != 0 || s == nullptr || s->group == 0 || s->spawned == nullptr) continue;
            ++spawners;
            for (uint16_t i = 0; i < s->max_alive; ++i) spawned_alive += s->spawned[i] != nullptr;
        }
        std::fprintf(f, "geradores %d modelos %u nascidos %u vivos %d levas %u\n", spawners,
                     g_drone_spawner_stats.templates, g_drone_spawner_stats.spawned, spawned_alive,
                     g_drone_spawner_stats.waves);
    }
    // Scripts de cena: quantos registrados e tocando, players, interruptores, a câmera de script.
    {
        int players = 0, switches = 0, fuseboxes = 0, playing = 0;
        for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
            if (o->type == 0x1b) {
                ++players;
                if (Script_IsPlaying(static_cast<SCRIPTINFO*>(SP_getScriptInfo(o))) != 0) ++playing;
            }
            switches += o->type == 0x39;
            fuseboxes += o->type == 0x3c;
        }
        std::fprintf(f, "scripts carregados %u instancias %u players %d tocando %d switches %d fusiveis %d entidades %u",
                     g_script_stats.loaded, g_script_stats.instances, players, playing, switches, fuseboxes,
                     g_script_stats.entities);
        const viewer_tag* sc = Script_ActiveCamera();
        if (sc != nullptr) {
            std::fprintf(f, " camera_script %08x pos %.2f %.2f %.2f frente %.3f %.3f %.3f fov %.3f", ScriptCam,
                         sc->camera.m[3][0], sc->camera.m[3][1], sc->camera.m[3][2], sc->camera.m[2][0],
                         sc->camera.m[2][1], sc->camera.m[2][2], sc->fov);
        } else {
            std::fprintf(f, " camera_script %08x", ScriptCam);
        }
        std::fprintf(f, "\n");
        // Personagens tocando o script de entrada (estado 3, NDrone2_DSTATE_PlayScript).
        for (const LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
            const Drone_tag* d = reinterpret_cast<const Drone_tag*>(n);
            if (d->sm.state != 3) continue;
            std::fprintf(f, "playscript %.2f %.2f %.2f script %08x danim %u impacto %x\n", d->obj->pos.x,
                         d->obj->pos.y, d->obj->pos.z, d->entry_script, d->danim, d->state_a);
        }
        // Interruptores: posição, canal, estado, quantas vezes viraram e o estado do script player.
        for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
            if (o->type != 0x39) continue;
            const auto* sw = static_cast<const SWITCH_tag*>(o->data);
            const auto* sp = sw->player != nullptr ? static_cast<const SCRIPTPLAYER_tag*>(sw->player->data) : nullptr;
            std::fprintf(f, "switch %.2f %.2f %.2f canal %x ligado %u estado %u usos %u sp %d\n", o->pos.x, o->pos.y,
                         o->pos.z, sw->channel, switch_channels[sw->channel & 0xff], o->state, sw->uses,
                         sp != nullptr ? int(sp->state) : -1);
        }
    }
    // Camada 2D: idioma, mensagens aceitas por tipo, sprites e o que o quadro desenhou; uma linha por
    // texto desenhado (pela origem dos glifos: posição na tela do PS2 e o texto) e por imagem.
    {
        int glyphs = 0, images = 0;
        for (const Psi2DQuad& q : g_psi_frame.quads2d) {
            glyphs += q.origin.kind == Psi2DKind::Glyph;
            images += q.origin.kind == Psi2DKind::Image;
        }
        std::fprintf(f, "2d idioma %s entradas %u legendas %u mensagens", SelLang >= 0 && SelLang < kLanguageCount ? FNames[SelLang] : "?",
                     NumEntries, DrawInfo[2]);
        for (int t = 1; t < kTextMsgTypes; ++t) std::fprintf(f, " %d:%u", t, g_text_stats.added[t]);
        std::fprintf(f, " linhas %u recusadas %u sprites %u quadrados %zu glifos %d imagens %d\n",
                     g_text_stats.lines, g_text_stats.refused, SpriteList.used.count,
                     g_psi_frame.quads2d.size(), glyphs, images);
        const auto quoted = [](const char* s) {
            std::string o;
            for (; s != nullptr && *s != 0; ++s) {
                if (*s == '\n') {
                    o += "\\n";
                } else if (uint8_t(*s) < 0x20 || uint8_t(*s) >= 0x7f) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\x%02x", uint8_t(*s));
                    o += buf;
                } else {
                    o += *s;
                }
            }
            return o;
        };
        const void* last = nullptr;
        for (const Psi2DQuad& q : g_psi_frame.quads2d) {
            if (q.origin.pass != Psi2DPass::Main) continue;
            if (q.origin.kind == Psi2DKind::Glyph && q.origin.sprite != last && q.origin.text != nullptr) {
                last = q.origin.sprite;
                std::fprintf(f, "texto2d %.1f %.1f fonte %u \"%s\"\n", q.x0, q.y0, q.origin.font, quoted(q.origin.text).c_str());
            } else if (q.origin.kind == Psi2DKind::Image) {
                std::fprintf(f, "imagem2d %.1f %.1f %.1f %.1f textura %08x\n", q.x0, q.y0, q.x1, q.y1, q.origin.texture_hash);
            }
        }
    }
    // As salas visíveis da câmera 0 estão na lista 0 dela, com as chaves -90000, -89990... (a lista
    // ligada em GfxList + 4 é de quem capturou por último).
    for (uint16_t i = 0; v != nullptr && i < v->counts[0]; ++i) {
        const DRAWENTRY_tag& e = v->lists[0][i];
        if (e.cel == nullptr || -80000.0f < e.key) continue;
        const cel_tag* c = e.cel;
        std::fprintf(f, "sala %d tipo %08x ceu %d\n", index_of(c), c->type, ((c->type >> 28) & 1) == 0 ? 1 : 0);
    }
    std::fclose(f);
}

// Só do port (--list-adapters, --bench): um texto na saída padrão. O nightfire.exe é um programa de janela: com a
// saída redirecionada (um pipe do bash, um arquivo) o identificador já existe; aberto de um console (cmd), ele se
// liga ao console do pai.
void console_write(const std::string& text) { os::console_write(text); }

// O mesmo com texto largo: no console, WriteConsoleW (os acentos e o "…" saem certos); redirecionado, UTF-8.
void console_write_w(const std::wstring& text) { os::console_write_w(text); }

std::wstring pc_text(PcText id) { return from_utf8(PcText_Utf8(id)); }

std::wstring pc_text_s(PcText id, const std::wstring& arg) {
    const std::wstring fmt = pc_text(id);
    const size_t at = fmt.find(L"%s");
    return at == std::wstring::npos ? fmt : fmt.substr(0, at) + arg + fmt.substr(at + 2);
}

// --pack-disc: a cópia pessoal em um arquivo só (platform/disc_pack.h), sem janela: o andamento e o resultado no console.
int run_pack_cli(const Options& opt) {
    PcText_SetLanguage(opt.language_pc);
    // A cópia pessoal só existe no Windows (platform/disc_pack.h: no macOS o programa é um Mach-O assinado num .app).
    if (!nf::personal_copy_supported()) {
        console_write("--pack-disc: a copia pessoal em um arquivo so existe no Windows (no macOS, use a imagem .iso/.cdr "
                      "ou a pasta com os arquivos do disco)\n");
        return 2;
    }
    const nf::DiscTitle& title = nf::nightfire_disc();
    const nf::DiscFound f = nf::probe_disc(opt.pack_disc, nf::DiscOrigin::Command, title);
    if (!f.ok()) {
        console_write_w(pc_text(PcText::SuErrTitle) + L": " + nf::ui::setup_problem_text(f.check) +
                        (f.check.detail.empty() ? std::wstring() : L" (" + from_utf8(f.check.detail) + L")") + L"\n");
        return 2;
    }
    const std::wstring exe = exe_path();
    const std::wstring out = opt.pack_out.empty() ? nf::default_pack_path(exe) : opt.pack_out;
    const std::wstring hd = UiHdApp::find_dir(opt.hd_ui.dir, f.source->folder());
    const nf::PackPlan plan = nf::pack_plan(*f.source, title, exe, opt.pack_movies, hd, "PORT/HD_UI/");
    console_write_w(pc_text(PcText::SuDescPackTitle) + L": " + f.source->location() + L" -> " + out + L"\n" +
                    std::to_wstring(plan.files.size()) + L" + " + std::to_wstring(plan.extras.size()) + L" (" +
                    nf::ui::setup_size_text(plan.bytes) + L")\n" + pc_text(PcText::SuPackWarning) + L"\n");
    nf::PackProgress progress;
    nf::PackError error = nf::PackError::None;
    std::string detail;
    std::atomic<bool> finished{false};
    bool ok = false;
    std::thread worker([&] {
        ok = nf::pack_disc(*f.source, title, plan, exe, out, &progress, &error, &detail);
        finished = true;
    });
    int shown = -1;
    while (!finished) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const uint64_t total = progress.total;
        const int pct = total == 0 ? 0 : int(double(progress.done) * 100.0 / double(total));
        if (pct / 10 != shown / 10) {
            shown = pct;
            wchar_t n[16];
            std::swprintf(n, 16, L"%d", pct);
            std::wstring line = pc_text(PcText::SuPacking);
            const size_t at = line.find(L"%d");
            if (at != std::wstring::npos) line = line.substr(0, at) + n + line.substr(at + 2);
            const size_t pp = line.find(L"%%");
            if (pp != std::wstring::npos) line.erase(pp, 1);
            console_write_w(line + L"\n");
        }
    }
    worker.join();
    if (!ok) {
        std::wstring why;
        switch (error) {
            case nf::PackError::InsideGitTree: why = pc_text(PcText::SuPackInGit); break;
            case nf::PackError::SameFile: why = pc_text(PcText::SuPackSame); break;
            case nf::PackError::NotVerified: why = pc_text(PcText::SuPackNoCnf); break;
            case nf::PackError::MissingFile: why = pc_text_s(PcText::SuPackMissing, from_utf8(detail)); break;
            case nf::PackError::NoSpace: why = pc_text_s(PcText::SuPackNoSpace, from_utf8(detail)); break;
            case nf::PackError::Write: why = pc_text_s(PcText::SuPackWrite, from_utf8(detail)); break;
            case nf::PackError::Read: why = pc_text_s(PcText::SuPackRead, from_utf8(detail)); break;
            case nf::PackError::Cancelled: why = pc_text(PcText::SuPackCancelled); break;
            default: break;
        }
        console_write_w(pc_text(PcText::SuPackFailTitle) + L": " + why + L"\n");
        return 3;
    }
    console_write_w(pc_text_s(PcText::SuPackDone, out) + L"\n" + pc_text(PcText::SuPackWarning) + L"\n");
    return 0;
}

// O relógio do limite de quadros, em segundos.
double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Espera até o prazo (now_seconds): platform/os.h (no Windows, um temporizador de alta resolução até ~1 ms antes, depois o
// processador cedido em volta; o Sleep comum arredonda para 15,6 ms).
void wait_until(double deadline) { os::wait_until(deadline); }

// Matriz de visão e projeção da câmera 0 do jogo (viewer_tag::camera: linha 1 para cima, linha 2
// para a frente, linha 3 a posição).
std::array<float, 16> view_projection(const MATRIX& cam, float fov_deg, float aspect, float z_near, float z_far) {
    const XMVECTOR eye = XMVectorSet(cam.m[3][0], cam.m[3][1], cam.m[3][2], 1);
    const XMVECTOR fwd = XMVectorSet(cam.m[2][0], cam.m[2][1], cam.m[2][2], 0);
    const XMVECTOR up = XMVectorSet(cam.m[1][0], cam.m[1][1], cam.m[1][2], 0);
    const XMMATRIX view = XMMatrixLookToRH(eye, fwd, up);
    // Profundidade invertida: passa far e near trocados (a névoa do renderizador desfaz isso com os dois).
    const XMMATRIX proj = XMMatrixPerspectiveFovRH(XMConvertToRadians(fov_deg), aspect, z_far, z_near);
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, view * proj);
    std::array<float, 16> out;
    std::memcpy(out.data(), &m, sizeof(out));
    return out;
}

// Só do port (--renderer d3d9ff): visão e projeção separadas, como a função fixa do Direct3D 9 (e o RTX Remix) as
// recebem por SetTransform; projeção comum (z de 0 perto a 1 longe), sem a profundidade invertida do Direct3D 11.
D3D9FrameParams d3d9_frame(const float cam[16], float fov_deg, float aspect, float z_near, float z_far) {
    const XMVECTOR eye = XMVectorSet(cam[12], cam[13], cam[14], 1);
    const XMVECTOR fwd = XMVectorSet(cam[8], cam[9], cam[10], 0);
    const XMVECTOR up = XMVectorSet(cam[4], cam[5], cam[6], 0);
    D3D9FrameParams p;
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, XMMatrixLookToRH(eye, fwd, up));
    std::memcpy(p.view.data(), &m, sizeof(m));
    XMStoreFloat4x4(&m, XMMatrixPerspectiveFovRH(XMConvertToRadians(fov_deg), aspect, z_near, z_far));
    std::memcpy(p.projection.data(), &m, sizeof(m));
    return p;
}

}  // namespace

int nf_app_main(int argc, wchar_t** argv) {
    try {
        Options opt = parse_args(argc, argv);
        // --list-adapters: os adaptadores de vídeo pelo DXGI (com o teste de memória unificada) e sai.
        if (opt.list_adapters) {
            std::string out = "adaptadores de video (DXGI; o indice e o de --adapter <n>):\n";
            const std::vector<nf::AdapterInfo> list = nf::enumerate_adapters(true);
            for (const nf::AdapterInfo& a : list) {
                char ranks[64];
                std::snprintf(ranks, sizeof(ranks), " | consumo %d desempenho %d", a.power_rank, a.perf_rank);
                out += "  " + nf::describe_adapter(a) + ranks + "\n";
            }
            nf::AdapterPreference pref;
            nf::parse_adapter_preference(opt.adapter, &pref);
            const int pick = nf::choose_adapter(list, pref);
            out += "preferencia " + opt.adapter + ": " +
                   (pick >= 0 ? std::to_string(list[size_t(pick)].index) + " \"" + list[size_t(pick)].name + "\""
                              : std::string("o padrao do sistema")) +
                   "\n";
            console_write(out);
            return 0;
        }
        // --pack-disc: a cópia pessoal em um arquivo só (platform/disc_pack.h), sem janela.
        if (!opt.pack_disc.empty()) return run_pack_cli(opt);
        // O idioma dos textos do port (a tela de configuração vem antes do resto).
        PcText_SetLanguage(opt.language_pc);
        // O disco do jogador (platform/disc_source.h): o da linha de comando sozinho, ou a procura (a cópia pessoal, a
        // pasta do executável, a atual, as de cima, o [game] disc do nightfire.ini, os leitores de DVD).
        const nf::DiscTitle& disc_title = nf::nightfire_disc();
        nf::DiscFound found;
        if (!opt.disc.empty()) {
            found = nf::probe_disc(opt.disc, nf::DiscOrigin::Command, disc_title);
        } else {
            nf::DiscPlaces places = nf::disc_places_here();
            places.settings = opt.disc_ini;
            found = nf::find_disc(places, disc_title);
        }
        // A tela de entrada (ui/modern_setup.h, launcher_reason): forçada (--setup, --launcher), sem um disco que sirva, a
        // cada partida sem argumentos com [ui] launcher ligado, ou com o Shift seguro (lido só quando decide).
        nf::ui::LauncherStart launcher_start;
        launcher_start.forced = opt.setup;
        launcher_start.disc_ok = found.ok();
        launcher_start.command_line = opt.command_line;
        launcher_start.show_at_startup = opt.show_launcher;
        launcher_start.shift = !opt.setup && found.ok() && !opt.command_line && !opt.show_launcher && os::key_held(nf::key::Shift);
        const nf::ui::LauncherReason launcher_reason = nf::ui::launcher_reason(launcher_start);
        const bool need_setup = launcher_reason != nf::ui::LauncherReason::None;
        if (need_setup && opt.d3d9ff) {
            os::show_error(L"Nightfire PC", pc_text(PcText::SuErrTitle) + L"\n" + nf::ui::setup_problem_text(found.check));
            return 1;
        }
        // As capturas e o --bench: só o roteiro de --input, sem som, sem VSync, o cartão em memória.
        const bool scripted = !opt.screenshot.empty() || opt.bench > 0.0f;

        Window window(L"Nightfire PC", opt.width, opt.height, opt.window_mode != nf::WindowMode::Windowed);
        // Capturas fora da tela, no tamanho pedido (a janela não passa da área de trabalho); o --bench com --offscreen.
        const bool offscreen = !opt.screenshot.empty() || (opt.bench > 0.0f && opt.offscreen);
        const int view_w = offscreen ? opt.width : window.width();
        const int view_h = offscreen ? opt.height : window.height();
        // --renderer d3d9ff (só do port; docs/remix.md): a janela do jogo fica com o Direct3D 9 de função fixa; o
        // Direct3D 11 continua existindo (as camadas 2D e o Direct2D usam o dispositivo dele) numa janela invisível,
        // desenhando fora da tela e sem apresentar.
        void* const d3d11_window = opt.d3d9ff ? create_remix_hidden_window(view_w, view_h) : window.native_handle();
        // O adaptador de vídeo (só do port; platform/gpu_adapter.h): auto fica com o padrão do sistema, como antes (o
        // dispositivo sem adaptador); os outros procuram na lista do DXGI (integrated e discrete precisam do teste de
        // memória unificada). Sem nenhum que sirva, o padrão, e o relatório diz.
        std::string adapter_note = "auto";
        bool adapter_requested = false;
        nf::AdapterInfo adapter_pick;
        {
            nf::AdapterPreference pref;
            nf::parse_adapter_preference(opt.adapter, &pref);
            if (pref.kind != nf::AdapterPrefKind::Auto) {
                const bool classify = pref.kind == nf::AdapterPrefKind::Integrated || pref.kind == nf::AdapterPrefKind::Discrete;
                const std::vector<nf::AdapterInfo> list = nf::enumerate_adapters(classify);
                const int pick = nf::choose_adapter(list, pref);
                if (pick >= 0) {
                    adapter_pick = list[size_t(pick)];
                    adapter_requested = true;
                }
            }
        }
        // O renderizador abre o adaptador escolhido (sem ele, ou se não abrir, o padrão do sistema).
        Renderer renderer(d3d11_window, view_w, view_h, opt.vsync && opt.screenshot.empty(), offscreen || opt.d3d9ff,
                          adapter_requested ? &adapter_pick : nullptr);
        if (nf::AdapterPreference pref; nf::parse_adapter_preference(opt.adapter, &pref) && pref.kind != nf::AdapterPrefKind::Auto) {
            adapter_note = opt.adapter + (renderer.adapter_chosen() ? std::string() : std::string(" (nao achado: o padrao)"));
        }

        // A tela de entrada (render/setup_app.h): o disco (a imagem, um leitor, a cópia pessoal), a resolução, o modo de
        // tela, o VSync, o estilo dos menus, a camada 2D em HD e "Show at startup".
        if (need_setup) {
            // Uma cópia pessoal sozinha numa pasta qualquer: o hd_ui do port que veio dentro dela (as fontes do estilo
            // Modern) sai antes da tela, como sairia antes do jogo.
            if (found.ok() && found.source->kind() == nf::DiscKind::Packed &&
                UiHdApp::find_dir(opt.hd_ui.dir, found.source->folder()).empty()) {
                opt.hd_ui.dir = nf::extract_packed_extras(*found.source, "PORT/HD_UI");
            }
            nf::SetupOptions so;
            so.window = &window;
            so.renderer = &renderer;
            so.hd_ui_dir = UiHdApp::find_dir(opt.hd_ui.dir, found.source != nullptr ? found.source->folder() : std::wstring());
            so.settings.video = video_settings_of(opt);
            so.settings.show_at_startup = opt.show_launcher;
            so.reason = launcher_reason;
            so.build = NF_PORT_BUILD;
            so.title = &disc_title;
            so.found = found;
            so.capture = !opt.screenshot.empty();
            so.screenshot = opt.screenshot;
            so.wait = opt.wait_given ? opt.wait : 1.0f;
            so.pick = opt.setup_pick;
            so.exe = exe_path();
            so.pack_movies = opt.pack_movies;
            // O roteiro das capturas (--setup-input, como o --input: teclas, o controle simulado, cmouse=x,y na tela
            // virtual de 1920 x 1080).
            const std::vector<InputStep> setup_script = parse_input(opt.setup_input);
            so.script = [&setup_script, &renderer](float t, nf::SetupFrameInput& in) {
                for (const InputStep& st : setup_script) {
                    if (st.has_mouse) {
                        const nf::ui::Canvas cv = nf::ui::make_canvas(renderer.width(), renderer.height());
                        in.mouse_inside = true;
                        in.mouse_x = st.mouse_canvas ? nf::ui::window_x(cv, st.mouse_x) : st.mouse_x;
                        in.mouse_y = st.mouse_canvas ? nf::ui::window_y(cv, st.mouse_y) : st.mouse_y;
                    }
                    if (t < st.seconds) {
                        for (const int vk : st.keys) in.keys[vk] = true;
                        if (st.uses_pad) {
                            in.pad = st.pad;
                            in.pad.connected = !st.pad_off;
                        }
                        return;
                    }
                    t -= st.seconds;
                }
            };
            // A escolha vai para o [game] disc do nightfire.ini (nas capturas sem --config, não).
            so.store = [&opt](const std::wstring& path) {
                if (!opt.can_save_settings) return false;
                nf::settings().set("game", "disc", to_utf8(path));
                return nf::settings().save();
            };
            const nf::SetupResult sr = nf::run_setup(so);
            if (sr.captured || sr.quit || !sr.found.ok()) return 0;
            found = sr.found;
            // PLAY com opções mudadas: gravadas no nightfire.ini com as chaves das opções do jogo (só quando o arquivo pode
            // ser escrito) e o jogo abre com elas, como se viessem do arquivo: a janela muda de tamanho ou de modo antes do
            // primeiro quadro (a tela cheia exclusiva liga logo abaixo, com a volta para a janela se não der).
            if (sr.settings_changed) {
                const nf::VideoSettings& v = sr.settings.video;
                opt.width = v.width;
                opt.height = v.height;
                opt.window_mode = v.window_mode;
                opt.vsync = v.vsync;
                opt.menu_style = v.menu_style;
                opt.hd_ui.enabled = v.hd_ui;
                opt.show_launcher = sr.settings.show_at_startup;
                if (opt.can_save_settings && nf::ui::store_launcher_settings(sr.settings, so.settings, nf::settings())) {
                    nf::settings().save();
                }
                if (!offscreen) {
                    window.set_fullscreen(opt.window_mode != nf::WindowMode::Windowed);
                    if (opt.window_mode == nf::WindowMode::Windowed) window.set_client_size(opt.width, opt.height);
                    renderer.set_vsync(opt.vsync && opt.screenshot.empty());
                    if (window.resized()) renderer.resize(window.width(), window.height());
                }
            }
        }
        const std::shared_ptr<nf::DiscSource> disc = found.source;
        // Uma cópia pessoal sozinha numa pasta qualquer: o hd_ui do port que veio dentro dela (as fontes e os glifos do
        // estilo Modern, o map.json da camada HD), tirado uma vez para a pasta de dados do usuário.
        UiHdOptions hd_ui_options = opt.hd_ui;
        if (disc->kind() == nf::DiscKind::Packed && UiHdApp::find_dir(hd_ui_options.dir, disc->folder()).empty()) {
            hd_ui_options.dir = nf::extract_packed_extras(*disc, "PORT/HD_UI");
        }
        // A linha do relatório das capturas.
        const std::string disc_line = "disco " + disc->describe() + " origem " + nf::disc_origin_name(found.origin) +
                                      " versao " + (found.check.boot_id.empty() ? std::string("-") : found.check.boot_id) +
                                      (found.check.unverified ? " (nao conferida)" : "");

        FilesBin files(disc);
        if (!install_files(files)) throw std::runtime_error("não consegui abrir o FILES.BIN");
        // O som: o driver do IOP lê a pasta PS2\ do disco; a saída é o XAudio2, um WAV (--audio-dump) ou nada
        // (--no-audio, e as capturas).
        nf::sfx_iop_set_disc(disc);
        // Os filmes (MOVIES\30_FPS do disco; platform/psi_movie.h): pelo [video] movies; nas capturas e no --bench só com
        // --movies ou --movie-frame (sem eles as páginas de filme seguem na hora, como antes).
        nf::movie_set_disc(disc);
        nf::movie_set_decoder_choice(opt.movie_decoder);
        nf::movie_set_enabled(opt.movie_frame >= 0 || opt.movies_flag == 1 ||
                              (opt.movies_flag != 0 && opt.movies && !scripted));
        if (!opt.audio_dump.empty()) {
            nf::audio_out_open_dump(opt.audio_dump);
        } else if (!opt.no_audio && !scripted) {
            nf::audio_out_open_device();
        }
        std::string tables_error;
        if (!install_game_tables(*disc, &tables_error)) {
            throw std::runtime_error("construtores estáticos do ACTION.ELF: " + tables_error);
        }
        // Depuração: todas as missões cumpridas com platina nos perfis padrão.
        if (opt.unlock_all) Menu_PortUnlockAll();
        std::vector<std::string> levels = list_levels(files);
        // Sem --level, o menu principal (07000048): o boot do jogo pede ele.
        const bool front_end = opt.level.empty();
        const std::string first_level = front_end ? "07000048" : opt.level;
        size_t level_index = 0;
        for (size_t i = 0; i < levels.size(); ++i) {
            if (levels[i] == first_level) level_index = i;
        }
        // Só do PC: o cartão de memória é uma pasta (saves\ ou --save-dir); nas capturas sem --save-dir, em memória.
        std::unique_ptr<nf::FolderCard> folder_card;
        if (!opt.save_dir.empty() || !scripted) {
            folder_card = std::make_unique<nf::FolderCard>(opt.save_dir.empty() ? nf::default_save_dir() : opt.save_dir);
            nf::set_card(folder_card.get());
        }
        // Texturas e malhas nascem durante o ResetMap_Load (psiCreateMapTextures/psiCreateEntityGfx).
        RuntimeGfx gfx(renderer);
        // A tela de carga (platform/psi_loading.h): apresentada só numa janela de verdade.
        g_loading.renderer = &renderer;
        g_loading.gfx = &gfx;
        g_loading.present = !offscreen && !opt.d3d9ff;
        g_loading.shot = opt.load_screen;
        g_loading.shot_blob = opt.load_screen_blob;
        g_psi_loading_hook = &loading_hook;
        g_port_no_load_screen = opt.no_load_screen ? 1 : 0;
        if (opt.no_env) {
            DrawSnow = 0;
            g_port_no_env = 1;
            g_port_no_load_screen = 1;
        }
        std::unique_ptr<RemixD3D9Renderer> d3d9;
        if (opt.d3d9ff) {
            d3d9 = create_remix_d3d9(window.native_handle(), view_w, view_h, opt.vsync && opt.screenshot.empty(),
                                     renderer.adapter_chosen() ? &renderer.adapter() : nullptr);
            gfx.set_mirror(d3d9.get());
            d3d9->set_record_hashes(!opt.d3d9_remix_conf.empty());
        }
        // Pacote de texturas HD (formato do PCSX2), se houver: cada textura carregada procura a sua.
        TexturePack hd_pack;
        hd_pack.open(opt.textures.empty() ? default_textures_dir(*disc) : opt.textures);
        if (!hd_pack.empty()) gfx.set_texture_pack(&hd_pack);
        renderer.use_hd(!opt.no_hd);
        // Camada 2D em alta definição (F4): hd_ui/map.json, substitutas e fontes.
        UiHdApp ui_hd;
        ui_hd.init(hd_ui_options, disc->folder(), renderer, gfx);
        if (nf::UiHdPromptSet prompts; nf::ui_hd_parse_prompt_set(opt.prompts, &prompts)) nf::UiHd_SetPromptSetting(prompts);
        // O estilo de menu Modern (src/ui; Direct2D sempre, independente da camada HD): as opções do PC por cima da
        // pausa quando [ui] menu_style = modern.
        ModernUiApp modern;
        modern.init(renderer, ui_hd.dir(), NF_PORT_BUILD);
        g_modern_app = &modern;
        // Os ícones da roda de armas: as imagens do dossiê do pacote do menu, lidas do FILES.BIN uma vez (as tabelas
        // ds_weapons/ds_gadgets já vieram do ELF).
        if (modern.available()) modern.load_wheel_icons(files);

        // Só do PC: as opções de vídeo (platform/video_settings.h) em vigor e o controlador com que a página
        // de opções do menu (engine/menu_pc_options.h) as aplica e grava no nightfire.ini.
        float fov = opt.fov;
        renderer.set_texture_filter(opt.filter);
        renderer.set_scene_quality(opt.render_scale, opt.msaa);
        renderer.set_alpha_to_coverage(opt.alpha_coverage);
        if (!offscreen && opt.window_mode == nf::WindowMode::Exclusive &&
            !renderer.set_exclusive_fullscreen(true, opt.width, opt.height) && !opt.d3d9ff) {
            // Só do PC: o modo exclusivo não abriu (o monitor recusou o modo, outra placa tem o monitor): o jogo começa
            // numa janela que cabe no monitor, em vez de ficar numa tela que não mostra nada. O nightfire.ini fica como
            // estava (a próxima partida tenta de novo; a tela de entrada troca o modo). O --renderer d3d9ff fica como era
            // (o Direct3D 11 dele não apresenta, então o modo exclusivo nunca abre ali).
            nf::VideoSettings v = video_settings_of(opt);
            v.window_mode = nf::WindowMode::Windowed;
            nf::fit_resolution_to_mode(v, renderer.display_modes(), renderer.desktop_mode());
            opt.window_mode = nf::WindowMode::Windowed;
            opt.width = v.width;
            opt.height = v.height;
            window.set_fullscreen(false);
            window.set_client_size(opt.width, opt.height);
            if (window.resized()) renderer.resize(window.width(), window.height());
        }
        nf::VideoController& video = nf::video_controller();
        {
            nf::VideoSettings live;
            live.width = opt.width;
            live.height = opt.height;
            live.window_mode = opt.window_mode;
            live.vsync = opt.vsync;
            live.render_scale = renderer.scene_scale();
            live.msaa = renderer.scene_msaa();
            live.filter = opt.filter;
            live.fov = opt.fov;
            live.hd_textures = renderer.using_hd();
            live.hd_ui = ui_hd.enabled();
            live.hud_edges = ui_hd.anchor_edges();
            live.language = PcText_Language();
            live.menu_style = opt.menu_style;
            live.fps_cap = opt.fps_cap;
            live.interpolation = opt.interpolation;
            live.instant_mouse_look = opt.instant_mouse;
            live.adapter = opt.adapter;
            video.init(live);
        }
        video.set_hardware(renderer.display_modes(), renderer.desktop_mode(), renderer.supported_msaa());
        // Os adaptadores da opção "Graphics adapter" (só os nomes: sem o teste de memória unificada, que custa um
        // dispositivo por placa) e o que o renderizador usa.
        video.set_adapters(nf::enumerate_adapters(false), renderer.adapter().name);
        video.set_store([&](const nf::VideoSettings& v) {
            if (!opt.can_save_settings) return false;  // captura sem --config: o nightfire.ini do jogador fica
            nf::store_video_settings(v, nf::settings());
            return nf::settings().save();
        });
        // O adaptador (next.adapter) não entra aqui: o dispositivo nasce uma vez; a escolha nova vale na próxima partida.
        video.set_apply([&](const nf::VideoSettings& next, const nf::VideoSettings& prev) {
            renderer.set_vsync(next.vsync && opt.screenshot.empty());
            renderer.use_hd(next.hd_textures);
            renderer.set_texture_filter(next.filter);
            renderer.set_scene_quality(next.render_scale, next.msaa);
            ui_hd.set_enabled(next.hd_ui);
            ui_hd.set_anchor_edges(next.hud_edges);
            fov = next.fov;
            // A janela (nas capturas a imagem tem o tamanho de --width/--height e a janela não muda).
            const bool display = next.width != prev.width || next.height != prev.height || next.window_mode != prev.window_mode;
            if (!offscreen && display) {
                if (prev.window_mode == nf::WindowMode::Exclusive) renderer.set_exclusive_fullscreen(false, 0, 0);
                window.set_fullscreen(next.window_mode != nf::WindowMode::Windowed);
                if (next.window_mode == nf::WindowMode::Windowed) window.set_client_size(next.width, next.height);
                if (next.window_mode == nf::WindowMode::Exclusive) {
                    renderer.set_exclusive_fullscreen(true, next.width, next.height);
                }
            }
            // O que não deu (MSAA sem suporte, sem memória, sem Direct2D) fica como ficou.
            nf::VideoSettings effective = next;
            effective.render_scale = renderer.scene_scale();
            effective.msaa = renderer.scene_msaa();
            effective.hd_ui = ui_hd.enabled();
            if (!(effective == next)) video.correct_live(effective);
        });
        // As teclas T e F4 mudam a configuração em vigor (sem gravar).
        const auto sync_toggles = [&]() {
            nf::VideoSettings v = video.live();
            v.hd_textures = renderer.using_hd();
            v.hd_ui = ui_hd.enabled();
            video.correct_live(v);
        };
        bool was_focused = true;
        MenuPc_Install();
        MenuCheats_Install();
        // A linha OPTIONS da pausa abre as opções Modern (com Direct2D e o estilo escolhido); senão, a página Original.
        MenuPc_SetOptionsHook([](uint8_t) {
            return g_modern_app != nullptr && nf::video_controller().live().menu_style == nf::MenuStyle::Modern &&
                   g_modern_app->open_last();
        });

        // --bench (só do port): os tempos de cada quadro medido e os custos de CPU somados desde o começo da medida.
        struct BenchState {
            bool started = false;
            bool skip = true;          // o próximo quadro não conta (o primeiro, ou depois de uma carga)
            double start = 0.0;        // o relógio no começo da medida
            double last_end = 0.0;     // o fim do último quadro
            std::vector<double> frame_ms;
            std::vector<float> submit_each, present_each, tick_each, draw_each;  // --bench-frames: por quadro
            double frame_submit = 0.0, frame_present = 0.0, frame_tick = 0.0, frame_draw = 0.0;
            double tick_ms = 0.0, draw_ms = 0.0, interp_ms = 0.0, submit_ms = 0.0, present_ms = 0.0;
            double movie_ms = 0.0;     // os filmes: decodificar (por tick e por quadro) e converter
            uint64_t ticks = 0, draws_game = 0;
            uint64_t items = 0;        // itens da lista de desenho, somados por quadro
            uint32_t level = 0;
        } bench;
        if (opt.bench > 0.0f) {
            renderer.set_profiling(true);
            renderer.set_offscreen_throttle(offscreen);
            // Na janela, por cima das outras: coberta, o Present não mostra nada (DXGI_STATUS_OCCLUDED) e a medida
            // não teria o custo de mostrar o quadro.
            if (!offscreen) window.set_topmost();
        }
        std::vector<DrawItem> draws;
        // A interpolação entre ticks (render/frame_interp.h): os registros dos dois últimos Game_Draw.
        FrameInterpolator interp;
        bool loaded = false;
        bool frame_ready = false;
        uint32_t loaded_frame = 0;
        Camera cam;
        bool prefer_free_cam = opt.has_cam;  // F2
        const std::vector<InputStep> script = parse_input(opt.input);
        psiMouseSensitivity = opt.sensitivity;
        // Só no port: as páginas de controles do PC no menu da pausa, e nas capturas um controle simulado
        // no lugar do XInput (conectado só se o roteiro usa pad_*).
        MenuPcControls_Register();
        static nf::ScriptedPadSource scripted_pad;
        bool script_pad = false;
        for (const InputStep& st : script) script_pad = script_pad || st.uses_pad;
        if (scripted) nf::set_pad_source(&scripted_pad);
        bool menu_page_opened = false;  // --open-menu-page
        int script_wheel_step = -1;     // o trecho de --input cuja roda já foi dada
        float esc_release_wait = 0.0f;  // Esc sem menu: segundos até soltar o mouse (nenhum menu abriu)
        int step = 1;  // direção para pular pacotes sem nada para desenhar (menu, tela de carga)
        size_t skipped = 0;
        bool first_load = true;
        uint32_t restarts_seen = 0;  // g_mission_port.restarts na última carga
        // O relógio do roteiro de --input: com --level, cada carga (fora os recomeços) o zera; no menu principal
        // ele conta da primeira carga (o roteiro atravessa o menu, a missão escolhida...).
        uint32_t first_loaded_frame = 0;
        bool first_load_frame_unset = true;
        uint32_t shot_level_loads = 0;  // cargas do pacote de --shot-level
        bool main_requested = false;  // a carga em andamento foi pedida pelo main (PgUp/PgDn, --level)

        // Pedidos de fase. ResetMap_LevelToLoad só aceita no estado de jogo (GF_WAIT_START), então o
        // pedido espera ali; sem filme de abertura (a fase pedida carrega direto).
        size_t pending_level = SIZE_MAX;
        const auto request_level = [&](size_t index, int direction) {
            pending_level = index;
            step = direction;
        };
        const auto start_pending_level = [&]() {
            if (pending_level == SIZE_MAX || GameFlow_GetState() != GF_WAIT_START) return;
            level_index = pending_level;
            pending_level = SIZE_MAX;
            loaded = false;
            main_requested = true;
            // --intro-movie: com o filme de abertura, como o mapa das missões pede (07000001 -> 07100001).
            ResetMap_LevelToLoad(uint32_t(std::stoul(levels[level_index], nullptr, 16)), 0, opt.intro_movie ? 0 : 1);
        };
        bool win_done = false;  // --win
        bool channels_done = false;  // --channel
        // Fim do ResetMap_Load: monta a lista de desenho a partir das estruturas do jogo.
        const auto on_level_loaded = [&]() {
            interp.reset();  // a fase nova não se interpola com a anterior (um corte)
            gfx.collect(draws);
            // A fase carregada (o menu principal e os menus do jogo também trocam de fase).
            {
                char name[16];
                std::snprintf(name, sizeof(name), "%08x", GameState.level_file);
                for (size_t i = 0; i < levels.size(); ++i) {
                    if (levels[i] == name) level_index = i;
                }
            }
            // Pacotes sem nada para desenhar (pedidos pelo main) são pulados; o menu principal não tem cena 3D e
            // não é pulado enquanto o menu dele está aberto.
            const bool front_end_menu = GameState.level_file == 0x7000048 && MenuManager_Exists(0) != 0;
            const bool skippable = main_requested;
            main_requested = false;
            if (skippable && draws.empty() && !front_end_menu && skipped + 1 < levels.size()) {
                ++skipped;
                request_level((level_index + levels.size() + size_t(step)) % levels.size(), step);
                return;
            }
            skipped = 0;
            float eye[3], target[3];
            gfx.suggested_view(draws, eye, target);
            cam.look_at(eye, target);
            if (g_player_init_call.called) cam.from_start(g_player_init_call.pos, g_player_init_call.rot);
            if (first_load && opt.has_cam) cam.look_at(&opt.cam[0], &opt.cam[3]);
            first_load = false;
            // O Player_Init liga a câmera 0 (Camera_Enable); sem ponto de partida não há jogador e a
            // câmera livre é a única.
            if (glb_players[0] == nullptr) Camera_Enable(0, 1, 1, nullptr);
            if (opt.has_at && glb_players[0] != nullptr) {
                _VECTOR pos{opt.at[0], opt.at[1], opt.at[2], 1.0f};
                _VECTOR rot{0.0f, opt.at[3], 0.0f, 0.0f};
                Player_StandAtNewPosition(glb_players[0], &pos, &rot, glb_players[0]->unknown_f6);
            }
            loaded = true;
            // Num recomeço pela missão fracassada (Mission_Update, no lugar do menu de fim de missão) o
            // roteiro de --input e o --wait continuam contando da carga original (não repetem; com o
            // jogador morrendo de novo antes do Enter do roteiro, repetiriam para sempre).
            if (g_mission_port.restarts == restarts_seen) loaded_frame = GameState.ticks;
            if (GameState.level_file == opt.shot_level) ++shot_level_loads;
            if (first_load_frame_unset) {
                first_loaded_frame = GameState.ticks;
                first_load_frame_unset = false;
            }
            restarts_seen = g_mission_port.restarts;
            frame_ready = false;  // espera o primeiro Game_Draw da fase
        };

        // Um tick de lógica: o GameFlow_Main portado. O desenho dele só marca o pedido (gancho draw); o Game_Draw
        // roda uma vez por tick, depois do tick (com a interpolação ligada, o padrão: o estado que o Game_Draw guarda
        // anda no ritmo do PS2), ou a cada quadro de vídeo com --no-interp (o comportamento antigo).
        static bool draw_requested = false;
        g_gameflow_hooks.draw = [] { draw_requested = true; };
        bool tick_to_draw = false;     // um tick rodou depois do último Game_Draw
        uint32_t tick_screen_blur = 0x80;  // o ScreenBlur do último Game_Draw (g_psi_frame.screen_blur)
        float tick_mouse_yaw = 0.0f;   // o giro do mouse que o Player_Update aplicou no último tick (radianos)
        float tick_mouse_pitch = 0.0f;
        const auto gameflow_main = [&]() {
            const uint32_t before = GameFlow_GetState();
            draw_requested = false;
            const uint32_t mouse_count = g_player_mouse_look.count;
            const double t0 = bench.started ? now_seconds() : 0.0;
            if (opt.force_lightning) {
                SuperHappyLightningFlag = 1;
                if (GameState.ticks % 10 == 0) Reflicker = 1;
            }
            GameFlow_Main();
            if (bench.started) {
                const double ms = (now_seconds() - t0) * 1000.0;
                bench.tick_ms += ms;
                bench.frame_tick += ms;
                ++bench.ticks;
            }
            tick_to_draw = true;
            // Somados até o próximo Game_Draw (normalmente um tick só).
            if (g_player_mouse_look.count != mouse_count) {
                tick_mouse_yaw += g_player_mouse_look.yaw;
                tick_mouse_pitch += g_player_mouse_look.pitch;
            }
            if (before == GF_BOOT && !front_end) {
                // O boot pediu o menu principal (Reset_MapLoadSettings); com --level o pedido é trocado pela fase
                // da linha de comando. No lugar do menu também: o idioma (P_LANGUAGE_Handler, sem tela de escolha
                // na versão americana: Txt_SetLanguage do LanguageToLoad) e a opção de legendas (DrawInfo[2]; o
                // PS2 americano começa com ela desligada, Menu_WillRunMiniMission; o port a liga, a não ser com
                // --no-subtitles). Sem --level o menu principal do jogo faz tudo isso (e as legendas seguem o
                // perfil do codinome).
                Txt_SetLanguage(LanguageToLoad);
                DrawInfo[2] = opt.subtitles ? 1 : 0;
                nf::game_options_rebase();  // --no-subtitles não vai para o .ini
                GameFlow_PopState();
                request_level(level_index, 1);
            }
            // O ResetMap_Load troca GF_LOAD_MAP por GF_LOCKS_INIT no fim da carga.
            // Um filme de abertura ou de fim (ResetMap_Load empilha GF_MOVIE no meio da carga) ainda não é a fase.
            if (before == GF_LOAD_MAP && GameFlow_GetState() != GF_LOAD_MAP && GameFlow_GetState() != GF_MOVIE) {
                on_level_loaded();
            }
            // --win: todos os objetivos cumpridos (o canal que Mission_MonitorObjectives liga), contando da carga da
            // primeira fase que não é o menu principal (sem --level, a missão que o roteiro escolhe).
            if (loaded && !win_done && 0.0f <= opt.win && GameState.level_file != 0x7000048 &&
                GameState.ticks - loaded_frame >= uint32_t(opt.win * 60.0f)) {
                win_done = true;
                switch_channels[100] = 1;
                switch_channels_time[100] = GameState.frame;
            }
            // --channel: os canais pedidos, uma vez, logo depois da carga (fora do menu principal).
            if (loaded && !channels_done && GameState.level_file != 0x7000048) {
                channels_done = true;
                for (const int ch : opt.channels) {
                    switch_channels[ch] = 1;
                    switch_channels_time[ch] = GameState.frame;
                }
                // --weapon: as armas pedidas, pelo caminho de uma arma apanhada (Player_EquipWeapon) e da troca
                // (next_weapon, a primeira da lista).
                if (!opt.give_weapons.empty() && glb_players[0] != nullptr && glb_players[0]->data != nullptr) {
                    BLData* gb = static_cast<BLData*>(glb_players[0]->data);
                    for (const int gw : opt.give_weapons) {
                        const int w = gw < 0 ? -gw : gw;
                        if (w <= 0 || w >= kWeaponSlots) continue;
                        const weapon_definition_tag* wd = weapon_def(w);
                        if (gw < 0) {
                            // "all": como a trapaça de todas as armas (CheatInfo + 4 em Player_InitAmmoWeapons), também
                            // as que não têm o modelo carregado na fase (no jogo um jogador não as tira: só no
                            // multijogador, Player_CheckWeaponsLoaded).
                            if (wd->selectable != 1 || wd->klass != w) continue;
                            gb->weapons[w].have = 1;
                            gb->weapons[w].rounds = wd->clip;
                            gb->ammo[wd->ammo_type] = 100;
                            continue;
                        }
                        Player_EquipWeapon(gb, int16_t(w), wd->clip);
                        gb->ammo[wd->ammo_type] = 100;
                    }
                    if (opt.give_weapons.front() > 0) anim_of(glb_players[0])->next_weapon = int8_t(opt.give_weapons.front());
                }
            }
            start_pending_level();
        };

        switch_ForceDrawAll = opt.draw_all ? 1 : 0;
        // O main do PS2 inicializa o heap antes do primeiro GameFlow_Main e, no primeiro boot
        // (PS2FreshBoot), pede o idioma americano.
        Mem_Init();
        LanguageToLoad = TXTLANG_USA;
        InitaliseMemoryCard();
        GameFlow_PushState(GF_BOOT, 0.0f, 0);
        GameCount = 0;  // global como no PS2: PS2_ResetFrameCount o acerta depois de carregar uma cena

        FrameParams frame;
        frame.lights = !opt.no_lights;
        frame.morphs = !opt.no_morphs;
        frame.env_map = !opt.no_env_map;
        frame.fog = !opt.no_fog;
        frame.debug_shading = opt.debug_shading;
        auto last = std::chrono::steady_clock::now();
        double fps_time = 0;
        int fps_frames = 0, frame_count = 0;
        float fps = 0;
        // O ritmo dos quadros (só do port): os quadros de vídeo (o relógio simulado de --fixed-video-hz), o VBlank
        // nominal do último tick (GameCount depois dele) e os VBlanks somados desde o último Game_Draw, a média de
        // quadros por tick (abaixo de 1.05 não há o que interpolar: cada quadro já é um tick), o giro do mouse do tick
        // do registro atual, o prazo do limite de quadros e as contas do custo da interpolação.
        uint64_t video_frames = 0;
        double tick_vblank = 0.0;
        uint32_t vblanks_since_draw = 0;
        int frames_since_tick = 0;
        float frames_per_tick = 1.0f;
        bool between_ticks = false;  // mais de um quadro de vídeo por tick: a interpolação tem o que mostrar
        float rec_mouse_yaw = 0.0f, rec_mouse_pitch = 0.0f;
        float slowmo_acc = 0.0f;     // plan_tick: a fração de tick ganha na câmera lenta da roda
        uint32_t slowmo_ticks = 0;   // os ticks rodados (o relatório da roda)
        bool interp_was_on = opt.interpolation;
        double cap_deadline = 0.0;
        double interp_ms_total = 0.0, interp_ms_max = 0.0;  // por quadro de vídeo: build + o olhar do mouse
        uint64_t interp_frames = 0;
        double push_ms_total = 0.0, push_ms_max = 0.0;  // por tick: guardar o registro e achar os pares
        uint64_t push_count = 0;
        // --frames-dump: os quadros seguidos gravados (-1: ainda não começou) e o personagem acompanhado.
        int dumped = -1;
        bool shot_taken = false;
        FILE* dump_tsv = nullptr;
        const obj_tag* dump_drone = nullptr;

        while (window.pump()) {
            const auto now = std::chrono::steady_clock::now();
            float dt = std::min(0.1f, std::chrono::duration<float>(now - last).count());
            last = now;

            // Ritmo da lógica como no main do PS2 (0x00109080): quadros de VBlank desde o último tick,
            // limitado a 2; a taxa resultante vai para GS_SetRefreshRate. No PC o desenho não espera
            // a lógica: a imagem roda na taxa do monitor, entre os dois últimos ticks (render/frame_interp.h).
            // --fixed-video-hz: o relógio é o de um monitor simulado, que avança 60 / N VBlanks por quadro.
            if (opt.fixed_video_hz > 0) {
                psi_set_clock_vblanks(double(video_frames) * 60.0 / double(opt.fixed_video_hz));
                dt = 1.0f / float(opt.fixed_video_hz);
            } else {
                psi_update_clock();
            }
            ++video_frames;
            // Com um filme tocando, o main do PS2 só roda a lógica nos VBlanks pares ((VBlankCount & ~1) - GameCount: 30
            // ticks por segundo) e decodifica o quadro antes de cada tick (psiDecodeBackgroundMovieFrame); entre os ticks
            // lê o arquivo adiante (psiSwapBGFMVFrame).
            const bool movie_tick = BGFMVPlaying != 0;
            const int32_t elapsed =
                movie_tick ? int32_t((VBlankCount & ~1u) - GameCount) : int32_t(VBlankCount - GameCount);
            // A câmera lenta da roda de armas (só do port): com ela aberta, só a fração [game] wheel_slowmo dos VBlanks
            // vira tick (plan_tick, render/frame_interp.h); a interpolação desenha entre eles e o som segue no tempo real.
            const float slowmo = g_wheel.slowmo() && !movie_tick ? std::clamp(nf::pc_gameplay().wheel_slowmo, 0.0f, 1.0f)
                                                                 : 1.0f;
            const TickPlan plan = plan_tick(elapsed, !loaded && !movie_tick, slowmo, &slowmo_acc);
            if (plan.due) {
                if (plan.resync) GameCount = VBlankCount;
                if (movie_tick) {
                    const double m0 = bench.started ? now_seconds() : 0.0;
                    psiDecodeBackgroundMovieFrame();
                    if (bench.started) bench.movie_ms += (now_seconds() - m0) * 1000.0;
                }
                GameCount += uint32_t(plan.consumed);
                vblanks_since_draw += uint32_t(plan.consumed);
                if (plan.run) {
                    GS_SetRefreshRate(60 / uint32_t(plan.skip), 60);
                    gameflow_main();
                    tick_vblank = double(GameCount);
                    ++slowmo_ticks;
                    // A média de quadros de vídeo por tick (uns 10 ticks), com histerese: a interpolação só entra com
                    // mais de 1,15 quadro por tick (monitor acima de ~69 Hz) e só sai abaixo de 1,08; a 60 Hz (um quadro
                    // por tick, com a deriva entre o monitor e o relógio) ela não liga e desligar não soma atraso.
                    frames_per_tick += (float(frames_since_tick) - frames_per_tick) * 0.1f;
                    frames_since_tick = 0;
                    if (frames_per_tick > 1.15f) between_ticks = true;
                    else if (frames_per_tick < 1.08f) between_ticks = false;
                } else {
                    // Sem tick nos VBlanks da câmera lenta: a SPU anda assim mesmo (o lote do driver só vai no próximo
                    // SFXUpdate), para o som não sair em rajadas.
                    nf::audio_out_catch_up(VBlankCount);
                }
            } else if (movie_tick) {
                psiSwapBGFMVFrame();
            }
            // Durante uma carga (GF_LOAD_MAP) as estruturas da fase anterior já foram apagadas (Mem_Init; no menu
            // principal glb_viewer só é zerado no passo 3 do ResetMap_Load): nada de câmera nem desenho, como no PS2. Um
            // filme no meio da carga (GF_MOVIE, depois do mundo da tela de carga) é desenhado, como no PS2.
            if ((!loaded && BGFMVPlaying == 0) || GameFlow_GetState() == GF_LOAD_MAP) {
                bench.skip = true;
                continue;
            }

            if (window.resized()) {
                renderer.resize(window.width(), window.height());
                // Só do PC: a janela redimensionada à mão (ou por set_client_size) é a resolução em vigor.
                if (!offscreen && video.live().window_mode == nf::WindowMode::Windowed && !window.fullscreen()) {
                    nf::VideoSettings v = video.live();
                    v.width = window.width();
                    v.height = window.height();
                    video.correct_live(v);
                }
            }
            // Alt+Enter alterna a janela sem bordas por conta própria: o modo em vigor acompanha.
            if (!offscreen && video.live().window_mode != nf::WindowMode::Exclusive &&
                window.fullscreen() != (video.live().window_mode == nf::WindowMode::Borderless)) {
                nf::VideoSettings v = video.live();
                v.window_mode = window.fullscreen() ? nf::WindowMode::Borderless : nf::WindowMode::Windowed;
                video.correct_live(v);
            }
            // Só do PC: a tela cheia exclusiva sai quando a janela perde o foco (Alt+Tab); volta com ele.
            const bool focused = window.focused();
            if (focused && !was_focused && !offscreen && video.live().window_mode == nf::WindowMode::Exclusive &&
                !renderer.exclusive_fullscreen()) {
                renderer.set_exclusive_fullscreen(true, video.live().width, video.live().height);
            }
            was_focused = focused;
            // Os textos das opções do PC com a camada HD, e a confirmação de vídeo sem menu aberto.
            MenuPc_Frame(ui_hd.enabled());
            // Os símbolos de botão da camada HD com [ui] prompts = auto seguem o último aparelho usado.
            nf::UiHd_SetPromptDevice(psiInput_ActiveDevice() == 1 ? nf::UiHdPromptSet::Xbox : nf::UiHdPromptSet::Keyboard);

            // Com --screenshot a entrada é ignorada: a janela aparece na frente e um clique ou uma tecla
            // mudaria a captura (o jogador recebe só as teclas de --hold e --use).
            viewer_tag* const view0 = glb_viewer[0];
            bool free_cam = prefer_free_cam || glb_players[0] == nullptr;
            bool want_shot = false;
            float mdx = 0.0f, mdy = 0.0f, wheel = 0.0f;
            g_psi_keyboard_mouse.active = false;
            nf::g_pad_enabled = scripted || window.focused();
            // --open-menu-page: a pausa e a página pedida, meio segundo depois da carga.
            if (opt.open_menu_page != 0 && !menu_page_opened && glb_players[0] != nullptr &&
                GameState.ticks - loaded_frame >= 30 && MenuManager_GetStatus() == 0) {
                menu_page_opened = true;
                MenuManager_Create(0x80000002u, 0x4000004b, 0, 0, 0x1e, 1, 3);
                GS_PauseGame(1);
                PcTab tab = PcTab::Video;
                nf::ui::Cat cat = nf::ui::Cat::Video;
                nf::ui::PauseScreen::Tab pause_tab = nf::ui::PauseScreen::Tab::Pause;
                const bool modern_style = video.live().menu_style == nf::MenuStyle::Modern && modern.available();
                if (opt.open_menu_page == kPcOptionsPage && modern_style) {
                    nf::ui::parse_cat(opt.menu_tab, &cat);
                    modern.open(cat);
                } else if (opt.open_menu_page == 0x4000004b && modern_style &&
                           nf::ui::PauseScreen::parse_tab(opt.menu_tab, &pause_tab)) {
                    modern.open_pause(pause_tab);
                } else if (opt.open_menu_page == kPcOptionsPage && MenuPc_ParseTab(opt.menu_tab, &tab)) {
                    MenuPc_ShowTab(0, tab);
                } else if (opt.open_menu_page != 0x4000004b) {
                    MenuPc_ShowPage(0, opt.open_menu_page);
                }
            }
            // Só do PC: com um menu aberto, o mouse fica solto e o cursor do Windows aparece enquanto o mouse é
            // usado (engine/menu_mouse.h); fechado o menu, a captura volta.
            const bool menu_open = MenuManager_GetStatus() != 0;
            g_psi_keyboard_mouse.cursor_inside = false;
            g_psi_keyboard_mouse.view_width = renderer.width();
            g_psi_keyboard_mouse.view_height = renderer.height();
            if (!scripted) {
                window.set_menu_mode(menu_open);
                window.set_cursor_visible(modern.active() ? modern.mouse_active() : psiInput_MenuMouse().active);
                if (menu_open) {
                    g_psi_keyboard_mouse.cursor_inside =
                        window.cursor_position(&g_psi_keyboard_mouse.cursor_x, &g_psi_keyboard_mouse.cursor_y);
                }
                if (window.key_pressed(key::Next)) request_level((level_index + 1) % levels.size(), 1);
                if (window.key_pressed(key::Prior)) request_level((level_index + levels.size() - 1) % levels.size(), -1);
                if (window.key_pressed('V')) frame.debug_shading = !frame.debug_shading;
                if (window.key_pressed(key::F3)) frame.wireframe = !frame.wireframe;
                if (window.key_pressed('H')) frame.show_volumes = !frame.show_volumes;
                if (window.key_pressed('P')) switch_ForceDrawAll = switch_ForceDrawAll == 0 ? 1 : 0;
                // --debug-menu: a página de depuração do jogo (as trapaças) por cima da pausa.
                if (opt.debug_menu && window.key_pressed(key::F9)) Menu_PortShowCheats(0);
                if (window.key_pressed('T')) {
                    renderer.use_hd(!renderer.using_hd());
                    sync_toggles();
                }
                if (window.key_pressed(key::F4)) {
                    ui_hd.toggle();
                    sync_toggles();
                }
                if (window.key_pressed(key::F2)) {
                    prefer_free_cam = !prefer_free_cam;
                    // A câmera livre começa onde o jogador está olhando.
                    if (prefer_free_cam && !free_cam && view0 != nullptr) cam.from_matrix(view0->camera);
                    free_cam = prefer_free_cam || glb_players[0] == nullptr;
                }
                const bool space = window.key_pressed(key::Space);
                want_shot = window.key_pressed(key::F12);
                // Só do PC: Esc é a pausa do jogo (o comando "pause"), e o menu solta o mouse (set_menu_mode). Na câmera
                // livre o jogo não recebe o teclado: Esc solta o mouse, como antes. Jogando, se nenhum menu abrir meio
                // segundo depois (uma cena, a morte, um carregamento: o jogo não abre a pausa), Esc também solta o mouse:
                // o cursor nunca fica preso sem saída (e Alt+Tab sempre solta).
                const bool esc = window.key_pressed(key::Escape);
                if (esc && window.mouse_captured() && !menu_open) {
                    if (free_cam && BGFMVPlaying == 0) {
                        window.release_mouse();
                    } else {
                        esc_release_wait = 0.5f;
                    }
                }
                if (esc_release_wait > 0.0f) {
                    if (MenuManager_GetStatus() != 0 || !window.mouse_captured()) {
                        esc_release_wait = 0.0f;
                    } else if ((esc_release_wait -= dt) <= 0.0f) {
                        window.release_mouse();
                        esc_release_wait = 0.0f;
                    }
                }

                window.take_mouse_delta(mdx, mdy);
                wheel = window.take_wheel();
                // Um menu aberto (pausa, fim de missão, menu principal) recebe o teclado também na câmera
                // livre (o menu principal não tem jogador).
                if (!free_cam || MenuManager_GetStatus() != 0 || BGFMVPlaying != 0) {
                    // O jogador: o estado do teclado e o mouse vão para o psiInput (lidos a cada tick). Num menu
                    // o mouse está solto e o teclado vale do mesmo jeito; num filme também (Start o pula: o GF_MOVIE
                    // das missões não tem jogador nem menu).
                    g_psi_keyboard_mouse.active =
                        window.mouse_captured() || ((menu_open || BGFMVPlaying != 0) && window.focused());
                    for (int vk = 0; vk < 256; ++vk) g_psi_keyboard_mouse.down[vk] = window.key_down(vk);
                    g_psi_keyboard_mouse.look_dx += mdx;
                    g_psi_keyboard_mouse.look_dy += mdy;
                    g_psi_keyboard_mouse.wheel += wheel;
                } else if (space) {
                    use_door_in_front(cam);
                }
            }
            if (scripted && (glb_players[0] != nullptr || MenuManager_GetStatus() != 0 || BGFMVPlaying != 0)) {
                // Capturas: só o roteiro de --input, e E nos primeiros ticks com --use (também com a
                // câmera livre, para filmar o jogador de fora).
                g_psi_keyboard_mouse.active = true;
                std::memset(g_psi_keyboard_mouse.down, 0, sizeof(g_psi_keyboard_mouse.down));
                nf::PadState pad;
                pad.connected = script_pad;
                const uint32_t script_frame = front_end ? first_loaded_frame : loaded_frame;
                float t = loaded || BGFMVPlaying != 0 ? float(GameState.ticks - script_frame) / 60.0f : -1.0f;
                bool has_mouse = false, mouse_canvas = false;
                float mouse_x = 0.0f, mouse_y = 0.0f;
                int step_index = -1;
                for (size_t i = 0; i < script.size(); ++i) {
                    const InputStep& st = script[i];
                    if (t < 0.0f) break;
                    if (st.has_mouse) {
                        has_mouse = true;
                        mouse_canvas = st.mouse_canvas;
                        mouse_x = st.mouse_x;
                        mouse_y = st.mouse_y;
                    }
                    if (t < st.seconds) {
                        step_index = int(i);
                        for (const int vk : st.keys) g_psi_keyboard_mouse.down[vk] = true;
                        // mlook=dx,dy: o mouse olhando, contagens por segundo vezes o tempo deste quadro de vídeo.
                        g_psi_keyboard_mouse.look_dx += st.look_dx * dt;
                        g_psi_keyboard_mouse.look_dy += st.look_dy * dt;
                        if (st.uses_pad) {
                            pad = st.pad;
                            pad.connected = !st.pad_off;
                        }
                        break;
                    }
                    t -= st.seconds;
                }
                scripted_pad.state[0] = pad;
                // O mouse do roteiro: o ponto da tela do PS2 levado à imagem (a área 4:3 de psi_2d_viewport) e a
                // roda uma vez, ao entrar no trecho.
                if (has_mouse && mouse_canvas) {
                    const nf::ui::Canvas cv = nf::ui::make_canvas(renderer.width(), renderer.height());
                    g_psi_keyboard_mouse.cursor_inside = true;
                    g_psi_keyboard_mouse.cursor_x = nf::ui::window_x(cv, mouse_x);
                    g_psi_keyboard_mouse.cursor_y = nf::ui::window_y(cv, mouse_y);
                } else if (has_mouse) {
                    const Psi2DViewport vp = psi_2d_viewport(renderer.width(), renderer.height());
                    g_psi_keyboard_mouse.cursor_inside = true;
                    g_psi_keyboard_mouse.cursor_x = vp.left + mouse_x * vp.scale_x;
                    g_psi_keyboard_mouse.cursor_y = vp.top + mouse_y * vp.scale_y;
                }
                if (step_index != script_wheel_step) {
                    script_wheel_step = step_index;
                    if (step_index >= 0) {
                        g_psi_keyboard_mouse.wheel += script[size_t(step_index)].wheel;
                        wheel = script[size_t(step_index)].wheel;
                    }
                }
                if (opt.use && loaded && GameState.ticks - loaded_frame < 10) g_psi_keyboard_mouse.down['E'] = true;
            }
            // Os filmes (platform/psi_movie.h): o quadro do relógio do som (decodificado agora o que faltar até ele) é o
            // fundo do quadro, como a cópia da imagem para o buffer de quadro do PS2 antes do resto do desenho, na tela
            // do PS2 (a área 4:3; a imagem centrada nos 512 x 448, como as tags de DecodeBGFMVFrame).
            nf::MovieImage movie_img;
            {
                const double mv0 = bench.started ? now_seconds() : 0.0;
                const bool shown = nf::movie_image(&movie_img);
                if (bench.started) bench.movie_ms += (now_seconds() - mv0) * 1000.0;
                if (shown) renderer.set_movie_frame(movie_img.bgra, movie_img.width, movie_img.height, movie_img.serial);
                const Psi2DViewport mvp = psi_2d_viewport(renderer.width(), renderer.height());
                const float ox = (512.0f - float(movie_img.width)) * 0.5f, oy = (448.0f - float(movie_img.height)) * 0.5f;
                const float rect[4] = {mvp.left + ox * mvp.scale_x, mvp.top + oy * mvp.scale_y,
                                       mvp.left + (ox + float(movie_img.width)) * mvp.scale_x,
                                       mvp.top + (oy + float(movie_img.height)) * mvp.scale_y};
                renderer.set_movie_background(shown, rect);
                // Os menus Modern: um filme de tela cheia (não em laço) passa por cima da tela; o fundo de uma página
                // fica atrás do desfoque no lugar da arte.
                modern.set_movie(shown, movie_img.looping);
                if (!shown) movie_img.index = -1;
            }
            // O estilo Modern: a tela de opções lê o teclado, o mouse e o controle deste quadro (o jogo e os menus
            // dele não recebem nada enquanto ela está aberta: psiInput_SetModal). Nas capturas o relógio é o dos
            // ticks (as animações saem iguais em toda captura).
            {
                ModernUiInput mi;
                mi.keys = g_psi_keyboard_mouse.active ? g_psi_keyboard_mouse.down : nullptr;
                mi.mouse_inside = g_psi_keyboard_mouse.cursor_inside;
                mi.mouse_x = g_psi_keyboard_mouse.cursor_x;
                mi.mouse_y = g_psi_keyboard_mouse.cursor_y;
                mi.wheel = wheel;
                float ui_dt = dt;
                if (!opt.screenshot.empty()) {
                    static uint32_t ui_ticks = GameState.ticks;
                    ui_dt = float(GameState.ticks - ui_ticks) / 60.0f;
                    ui_ticks = GameState.ticks;
                }
                // As telas Modern seguem os menus do jogo (a pausa, o fim de missão, o resultado) e só abrem com o
                // estilo Modern em vigor.
                modern.frame(ui_dt, mi, video.live().menu_style == nf::MenuStyle::Modern);
                // A roda de armas (engine/weapon_wheel.h): o comando seguro (o teclado e o controle da tabela de teclas),
                // o movimento do mouse deste quadro (aberta, ele é dela: não chega ao jogo) e o analógico direito. Só com o
                // Direct2D dos menus Modern (o desenho dela) e fora do caminho do Direct3D 9.
                if (modern.available() && d3d9 == nullptr) {
                    nf::InputFrame wf;
                    wf.keys = g_psi_keyboard_mouse.active ? g_psi_keyboard_mouse.down : nullptr;
                    wf.pad = psiInput_PlayerPad();
                    wf.pad_deadzone = nf::controls().pad_deadzone;
                    static const int wheel_cmd = nf::find_bind_command("weapon_wheel");
                    const bool held = wheel_cmd >= 0 && nf::command_value(nf::controls().bindings, wheel_cmd, wf) > 0.5f;
                    float wdx = 0.0f, wdy = 0.0f, sx = 0.0f, sy = 0.0f;
                    if (g_wheel.open() || held) {
                        wdx = g_psi_keyboard_mouse.look_dx;
                        wdy = g_psi_keyboard_mouse.look_dy;
                    }
                    if (const nf::PadState* wp = psiInput_PlayerPad()) {
                        sx = std::clamp(float(wp->rx) / 32767.0f, -1.0f, 1.0f);
                        sy = std::clamp(float(wp->ry) / 32767.0f, -1.0f, 1.0f);
                    }
                    g_wheel.frame(ui_dt, held, wdx, wdy, sx, sy, glb_players[0]);
                    if (g_wheel.open()) {
                        g_psi_keyboard_mouse.look_dx = 0.0f;
                        g_psi_keyboard_mouse.look_dy = 0.0f;
                    }
                    psiInput_SetWheelOpen(g_wheel.open());
                }
            }
            // QUIT GAME no menu principal Modern (confirmado no diálogo dele): sai como fechar a janela.
            if (modern.take_quit()) break;
            // O 2D do jogo por baixo de uma tela Modern some do quadro (o do menu; na pausa e no resultado, o HUD).
            gfx.set_hidden_2d(modern.hidden_2d());
            if (free_cam && !scripted) {
                cam.yaw -= mdx * opt.sensitivity;
                cam.pitch = std::clamp(cam.pitch - mdy * opt.sensitivity, -1.55f, 1.55f);
                cam.speed *= nf::fm::pow(1.25f, wheel);

                const XMVECTOR fwd = cam.forward();
                const XMVECTOR right = XMVector3Normalize(XMVector3Cross(fwd, XMVectorSet(0, 1, 0, 0)));
                XMVECTOR move = XMVectorZero();
                if (window.key_down('W')) move += fwd;
                if (window.key_down('S')) move -= fwd;
                if (window.key_down('D')) move += right;
                if (window.key_down('A')) move -= right;
                if (window.key_down('E')) move += XMVectorSet(0, 1, 0, 0);
                if (window.key_down('Q')) move -= XMVectorSet(0, 1, 0, 0);
                float speed = cam.speed;
                if (window.key_down(key::Shift)) speed *= 4;
                if (window.key_down(key::Control)) speed *= 0.25f;
                if (!XMVector3Equal(move, XMVectorZero())) {
                    XMStoreFloat3(&cam.pos, XMLoadFloat3(&cam.pos) + XMVector3Normalize(move) * speed * dt);
                }
            }

            const float aspect = float(renderer.width()) / float(renderer.height());
            // O campo de visão desenhado da câmera 0 do jogo: o zoom do jogo (viewer_tag::lod_scale = BLData::zoom, que o
            // PS2 multiplica na escala da projeção em PS2WorldViewVU1Kick2; antes o port o ignorava e a luneta não
            // aproximava nada) vezes o zoom leve da mira moderna (engine/player_ads.h, só do port). A câmera do jogo
            // (view0->fov) continua com o campo inteiro: a visibilidade fica a do PS2.
            const auto zoomed_fov = [&](float fov_deg) {
                float z = view0 != nullptr ? view0->lod_scale : 1.0f;
                if (!(z >= 1.0f)) z = 1.0f;
                z = std::min(z, 64.0f) * ads_view_zoom();
                if (z <= 1.0f) return fov_deg;
                return XMConvertToDegrees(2.0f * nf::fm::atan(nf::fm::tan(XMConvertToRadians(fov_deg) * 0.5f) / z));
            };
            // O quadro do jogo. Na câmera livre ela é a câmera 0: o Game_Run atualiza as câmeras a
            // cada tick (e põe a 0 nos olhos do jogador), então aqui a 0 volta para a câmera livre a
            // cada quadro de vídeo, sem a função de acompanhamento, e a sala dela acompanha o
            // movimento. Jogando, a câmera é a do jogo; só o campo de visão e a proporção são da
            // janela (no PS2 Camera_CalcViewAngles fixa 60 graus e 4:3 ou 16:9). O Game_Draw registra
            // em g_psi_frame o que desenhar.
            if (view0 != nullptr && free_cam) {
                // Só no port: na câmera livre o corpo do jogador aparece (o jogo o esconde da câmera
                // dele; Player_Update volta a esconder a cada tick).
                if (glb_players[0] != nullptr) View_SetDrawInAllViews(glb_players[0]);
                set_game_camera(view0, cam, fov, aspect);
                const auto updater = view0->updater;
                view0->updater = nullptr;
                Camera_Update(0);
                view0->updater = updater;
                Camera_CheckLocation(0);
            } else if (view0 != nullptr) {
                view0->fov = XMConvertToRadians(fov);
                view0->aspect = aspect;
            }
            // Numa cena com câmera (Script_CameraStart), o jogo desliga a câmera 0 e desenha pela 4; o
            // campo de visão vertical é o dela (o do quadro-chave), com a proporção da janela.
            const viewer_tag* scene_cam = Script_ActiveCamera();
            // O Game_Draw: com a interpolação (o padrão), uma vez por tick, como no PS2 a 60 quadros por segundo; o
            // registro do quadro (collect_frame) vai para o FrameInterpolator com a câmera do tick. Com --no-interp (ou a
            // opção desligada), a cada quadro de vídeo, como antes.
            const nf::VideoSettings& vs = video.live();
            const bool interp_mode = vs.interpolation;
            if (interp_mode != interp_was_on) {
                interp.reset();
                interp_was_on = interp_mode;
            }
            if (draw_requested && (!interp_mode || tick_to_draw || !frame_ready)) {
                const double draw_t0 = bench.started ? now_seconds() : 0.0;
                Game_Draw();
                tick_screen_blur = g_psi_frame.screen_blur;
                tick_to_draw = false;
                gfx.collect_frame(scene_cam != nullptr ? scene_cam : glb_viewer[0], draws);
                ui_hd.after_collect(renderer);
                frame_ready = true;
                if (interp_mode) {
                    const auto p0 = std::chrono::steady_clock::now();  // o custo do registro (a cópia e os pares)
                    TickRecord rec;
                    rec.items = draws;
                    const viewer_tag* cv = scene_cam != nullptr ? scene_cam : view0;
                    if (cv != nullptr) std::memcpy(rec.camera.m.data(), cv->camera.m, sizeof(float) * 16);
                    rec.camera.fov_deg = scene_cam != nullptr ? XMConvertToDegrees(scene_cam->fov)
                                         : free_cam              ? fov
                                                                 : zoomed_fov(fov);
                    rec.scene_camera = scene_cam != nullptr;
                    rec.ticks = float(std::max<uint32_t>(1, vblanks_since_draw));
                    // Os objetos que o tick atual ainda desenharia nesta câmera (vivos, visíveis nela): os que o tick
                    // anterior via e este não vê mais continuam no quadro (FrameInterpolator::push).
                    const uint16_t mask = scene_cam != nullptr ? uint16_t(1u << 4) : uint16_t(1u);
                    std::vector<const obj_tag*> live;
                    for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
                        if ((o->life & 1) == 0 && (o->unknown_f8 & mask) != 0 && (o->flags & 0x8010) == 0) live.push_back(o);
                    }
                    std::sort(live.begin(), live.end());
                    interp.push(std::move(rec), [&live](const void* owner, uint32_t created) {
                        const auto* o = static_cast<const obj_tag*>(owner);
                        return std::binary_search(live.begin(), live.end(), o) && o->created == created;
                    });
                    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p0).count();
                    push_ms_total += ms;
                    push_ms_max = std::max(push_ms_max, ms);
                    ++push_count;
                    rec_mouse_yaw = tick_mouse_yaw;
                    rec_mouse_pitch = tick_mouse_pitch;
                }
                tick_mouse_yaw = tick_mouse_pitch = 0.0f;
                vblanks_since_draw = 0;
                if (bench.started) {
                    const double ms = (now_seconds() - draw_t0) * 1000.0;
                    bench.draw_ms += ms;
                    bench.frame_draw += ms;
                    ++bench.draws_game;
                }
            }
            if (!frame_ready) {
                bench.skip = true;
                continue;
            }
            ++frames_since_tick;
            // O quadro: o registro do último tick (sem interpolação) ou o quadro entre os dois últimos.
            const std::vector<DrawItem>* items = &draws;
            float alpha = 1.0f;
            bool trick = false;
            InterpCamera shown;  // a câmera desenhada (o relatório e o --frames-dump)
            if (interp_mode && !interp.empty()) {
                // alpha: a fração do tick em curso. Nas capturas sem --fixed-video-hz, o tick inteiro (alpha 1: a imagem
                // é a do tick); com um quadro por tick ou menos (60 Hz ou abaixo), também (só somaria atraso).
                if (opt.interp_alpha >= 0.0f) {
                    alpha = opt.interp_alpha;
                } else if ((!opt.screenshot.empty() && opt.fixed_video_hz == 0) || !between_ticks) {
                    alpha = 1.0f;
                } else {
                    alpha = tick_alpha(psi_clock_vblanks(), tick_vblank, interp.latest().ticks);
                }
                const auto t0 = std::chrono::steady_clock::now();
                items = &interp.build(alpha, &shown);
                // A câmera livre anda a cada quadro de vídeo: a de agora, sem interpolação.
                if (free_cam && scene_cam == nullptr && view0 != nullptr) {
                    std::memcpy(shown.m.data(), view0->camera.m, sizeof(float) * 16);
                    shown.fov_deg = fov;
                }
                // Só do port: o olhar do mouse sem o atraso da interpolação. O giro que o tick atual já aplicou e a
                // interpolação ainda não mostra ((1 - alpha) dele) mais o que chegou depois do tick (o próximo
                // Player_Update o aplicará), na câmera e nos desenhos presos a ela (a lista 2: a arma e o clarão). Só
                // jogando em primeira pessoa, sem câmera de script, sem menu e com o jogador olhando pelo mouse.
                if (vs.instant_mouse_look && !free_cam && scene_cam == nullptr && MenuManager_GetStatus() == 0 &&
                    glb_players[0] != nullptr) {
                    bool yaw_ok = false, pitch_ok = false, aiming = false;
                    float zoom = 1.0f;
                    if (Player_PortMouseLookGate(glb_players[0], &yaw_ok, &pitch_ok, &aiming, &zoom)) {
                        float py = 0.0f, pp = 0.0f;
                        psiInput_PeekMouseLook(0, &py, &pp, aiming, zoom);
                        const float rest = interp.cut() ? 0.0f : 1.0f - std::clamp(alpha, 0.0f, 1.0f);
                        const float dyaw = (yaw_ok ? py : 0.0f) + rest * rec_mouse_yaw;
                        const float dpitch = (pitch_ok ? pp : 0.0f) + rest * rec_mouse_pitch;
                        if (dyaw != 0.0f || dpitch != 0.0f) {
                            float W[9];
                            camera_look_delta(shown.m.data(), dyaw, dpitch, 1.5707964f, W);
                            const float eye[3] = {shown.m[12], shown.m[13], shown.m[14]};
                            interp.rotate_overlay(W, eye);
                            trick = true;
                        }
                    }
                }
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                if (bench.started) bench.interp_ms += ms;
                interp_ms_total += ms;
                interp_ms_max = std::max(interp_ms_max, ms);
                ++interp_frames;
                MATRIX cm;
                std::memcpy(cm.m, shown.m.data(), sizeof(float) * 16);
                frame.view_proj = view_projection(cm, shown.fov_deg, aspect, frame.z_near, frame.z_far);
            } else if (scene_cam != nullptr) {
                std::memcpy(shown.m.data(), scene_cam->camera.m, sizeof(float) * 16);
                shown.fov_deg = XMConvertToDegrees(scene_cam->fov);
                frame.view_proj = view_projection(scene_cam->camera, XMConvertToDegrees(scene_cam->fov), aspect,
                                                  frame.z_near, frame.z_far);
            } else {
                if (view0 != nullptr) std::memcpy(shown.m.data(), view0->camera.m, sizeof(float) * 16);
                shown.fov_deg = free_cam ? fov : zoomed_fov(fov);
                frame.view_proj = view_projection(view0 != nullptr ? view0->camera : MATRIX{}, shown.fov_deg, aspect,
                                                  frame.z_near, frame.z_far);
            }
            // Câmera livre (só do port, depuração): a arma da lista 2 fica na cabeça do jogador (Player_PositionGun),
            // não diante desta câmera; sem a limpeza do Z que o jogo faz antes dela, a arma aparece onde está no
            // mundo em vez de por cima de tudo.
            std::vector<DrawItem> no_clear;
            if (free_cam && scene_cam == nullptr) {
                no_clear.reserve(items->size());
                for (const DrawItem& d : *items) {
                    if (!d.clear_depth) no_clear.push_back(d);
                }
                items = &no_clear;
            }
            std::vector<DrawItem> only_2d;  // --only-2d (depuração): sem os itens 3D
            if (opt.only_2d) {
                for (const DrawItem& d : *items) {
                    if (d.ui != nullptr || !d.quads2d.empty()) only_2d.push_back(d);
                }
                items = &only_2d;
            }
            const double submit_t0 = bench.started ? now_seconds() : 0.0;
            if (bench.started) bench.items += items->size();
            renderer.gpu_frame_begin();
            if (d3d9 != nullptr) {
                // --renderer d3d9ff: a mesma lista pelo Direct3D 9 de função fixa, com a câmera desenhada (shown).
                D3D9FrameParams p9 = d3d9_frame(shown.m.data(), shown.fov_deg, aspect, frame.z_near, frame.z_far);
                p9.fog = frame.fog;
                p9.wireframe = frame.wireframe;
                p9.show_volumes = frame.show_volumes;
                p9.game_lights = opt.d3d9_lights;
                p9.viewmodel_max_z = opt.d3d9_viewmodel;
                d3d9->render(*items, p9);
            } else if (modern.active()) {
                // Os menus Modern: a cena (o 2D escondido já ficou de fora), desfocada, o 2D que fica (a cortina de
                // sangue do fim de missão) nítido por cima e a tela.
                std::vector<DrawItem> scene, overlay;
                scene.reserve(items->size());
                for (const DrawItem& d : *items) {
                    if (d.ui == nullptr && d.quads2d.empty()) {
                        scene.push_back(d);
                    } else {
                        overlay.push_back(d);
                    }
                }
                renderer.render(scene, frame);
                renderer.screen_blur(0.0f);  // com um menu Modern, sem rastro (o quadro anterior é esquecido)
                modern.draw(overlay, frame);
            } else {
                renderer.render(*items, frame);
                // O rastro da tela (ScreenBlur, o CreateSqueezeFrame do PS2): o quadro anterior pesa 1 - ScreenBlur / 128
                // por tick a 60 Hz; por quadro de vídeo, essa fração elevada aos VBlanks do quadro (o mesmo rastro por
                // segundo em qualquer taxa).
                const float fresh = std::min(1.0f, float(tick_screen_blur) / 128.0f);
                float keep = 1.0f - fresh;
                if (keep > 0.0f) keep = nf::fm::pow(keep, std::max(0.0f, dt * 60.0f));
                renderer.screen_blur(keep);
                // Só do PC: numa janela mais larga que 4:3, uma luneta do HUD (as panes 6, 11, 17, 18 e 19, que no PS2 cobrem
                // a tela inteira, aqui a área 4:3 em peças) ganha faixas pretas dos lados: com o zoom agora desenhado, o mundo
                // aproximado apareceria fora dela. Fora de uma cena com câmera.
                if (modern.available() && scene_cam == nullptr && glb_players[0] != nullptr && glb_players[0]->data != nullptr) {
                    const auto* hb = static_cast<const BLData*>(glb_players[0]->data);
                    const auto* hud = static_cast<const HUDINFO_tag*>(hb->hud);
                    bool scope = false;
                    for (const uint32_t pane : {kHudSight, kHudCamera, kHudOICW, kHudRonin, kHudLaser}) {
                        if (hud != nullptr && hud->panes[pane].valid != 0 && hud->panes[pane].enabled != 0) scope = true;
                    }
                    const Psi2DViewport vp = psi_2d_viewport(renderer.width(), renderer.height());
                    if (scope && vp.left > 0.5f) {
                        const nf::ui::Canvas bcv = nf::ui::make_canvas(renderer.width(), renderer.height());
                        const float x0 = nf::ui::canvas_x(bcv, vp.left);
                        const float x1 = nf::ui::canvas_x(bcv, vp.left + vp.width);
                        nf::ui::DrawList bars;
                        bars.fill(nf::ui::Rect{bcv.left - 1.0f, bcv.top - 1.0f, x0, bcv.bottom + 1.0f}, nf::ui::rgb(0x000000));
                        bars.fill(nf::ui::Rect{x1, bcv.top - 1.0f, bcv.right + 1.0f, bcv.bottom + 1.0f}, nf::ui::rgb(0x000000));
                        renderer.draw_modern(bars, bcv);
                    }
                }
                // A roda de armas por cima do jogo e do HUD (o Direct2D dos menus Modern).
                if (g_wheel.visible()) {
                    // A tecla da roda: com o controle, o glifo do botão da tabela; senão a primeira tecla.
                    std::wstring wkey;
                    std::string wglyph;
                    const int wc = nf::find_bind_command("weapon_wheel");
                    if (wc >= 0) {
                        nf::InputCode list[nf::kMaxBinds];
                        const bool pad_dev = psiInput_ActiveDevice() == 1;
                        if (pad_dev && nf::controls().bindings.list(wc, true, list, nf::kMaxBinds) > 0) {
                            wglyph = nf::ui::xbox_glyph(list[0]);
                        }
                        if (wglyph.empty() && nf::controls().bindings.list(wc, false, list, nf::kMaxBinds) > 0) {
                            wkey = nf::ui::widen(nf::input_label(list[0], true));
                        }
                    }
                    modern.draw_wheel(nf::ui::make_wheel_view(g_wheel, wkey, wglyph), frame);
                }
            }
            renderer.gpu_frame_end();
            if (bench.started) {
                bench.frame_submit = (now_seconds() - submit_t0) * 1000.0;
                bench.submit_ms += bench.frame_submit;
            }
            ++frame_count;

            // A linha do ritmo dos quadros no relatório e no --frames-dump.
            const auto pacing_line = [&]() {
                const InterpStats& st = interp.stats();
                int sky_flash = 0;  // elementos do céu com modelo de relâmpago
                for (const SKYOBJ_tag& s : GfxList.sky) sky_flash += s.glist_flash != nullptr || s.obj_flash != nullptr;
                char line[640];
                std::snprintf(line, sizeof(line),
                              "ritmo interp %d alpha %.4f corte %d itens %u pares %u movendo %u saltos %u mantidos %u "
                              "ossos %u custo_ms %.4f max_ms %.4f tick_ms %.4f tick_max_ms %.4f quadros_por_tick %.2f "
                              "limite %d mouse_instantaneo %d "
                              "olhar_extra %d camera %.3f %.3f %.3f giro %.3f elevacao %.3f fov %.2f | ticks %u desenhos %u "
                              "ceu_gira %u relampagos %u/%d video_hz %d",
                              interp_mode ? 1 : 0, double(alpha), interp.cut() ? 1 : 0, st.items, st.matched, st.moving,
                              st.snapped, st.kept, st.bones,
                              interp_frames != 0 ? interp_ms_total / double(interp_frames) : 0.0, interp_ms_max,
                              push_count != 0 ? push_ms_total / double(push_count) : 0.0, push_ms_max,
                              double(frames_per_tick), vs.fps_cap, vs.instant_mouse_look ? 1 : 0, trick ? 1 : 0,
                              double(shown.m[12]), double(shown.m[13]), double(shown.m[14]),
                              double(XMConvertToDegrees(camera_yaw(shown.m.data()))),
                              double(XMConvertToDegrees(camera_elevation(shown.m.data()))), double(shown.fov_deg),
                              GameState.ticks - loaded_frame, GameState.draws, View_PortSkySpin(), View_PortLightnings(),
                              sky_flash, opt.fixed_video_hz);
                return std::string(line);
            };

            if (opt.use && free_cam && frame_count == 1) use_door_in_front(cam);
            // --movie-frame: no quadro n do filme (de --movie, se veio), no lugar do --wait e do --shot-level.
            const bool movie_shot = opt.movie_frame >= 0 && movie_img.index >= opt.movie_frame &&
                                    (opt.movie_code == 0 || movie_img.code == opt.movie_code);
            const bool shot_now = !opt.screenshot.empty() && frame_count >= 3 &&
                                  (opt.movie_frame >= 0
                                       ? movie_shot
                                       : (opt.shot_level == 0 || (GameState.level_file == opt.shot_level &&
                                                                  shot_level_loads >= opt.shot_level_count)) &&
                                             GameState.ticks - loaded_frame >= uint32_t(opt.wait * 60.0f));
            // --frames-dump <pasta> --frames N: a partir do quadro da captura, N quadros seguidos (PNG e uma linha de
            // frames.tsv cada: o relógio, o tick, o alpha, a câmera desenhada, o personagem mais perto como foi desenhado
            // e o custo da interpolação).
            if ((shot_now || dumped >= 0) && opt.frames > 0 && !opt.frames_dump.empty()) {
                if (dumped < 0) {
                    dumped = 0;
                    std::filesystem::create_directories(opt.frames_dump);
                    dump_tsv = os::open_file(os::join(opt.frames_dump, L"frames.tsv"), "w");
                    if (dump_tsv != nullptr) {
                        std::fprintf(dump_tsv, "quadro\tt\ttick\talpha\tcorte\tcam_x\tcam_y\tcam_z\tgiro\televacao\tfov\t"
                                               "ia_x\tia_y\tia_z\titens\tpares\tmovendo\tmantidos\tossos\tcusto_ms\t"
                                               "desenhos\tceu_gira\trelampagos\tcena\trosto_dist\trosto_pele\t"
                                               "rosto_estado\trosto_pesos\n");
                    }
                    // O personagem desenhado neste quadro mais perto da câmera.
                    float best = 1e30f;
                    for (const LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
                        const obj_tag* o = reinterpret_cast<const Drone_tag*>(n)->obj;
                        bool drawn = false;
                        for (const DrawItem& it : *items) drawn = drawn || (it.interp.owner == o && it.mesh >= 0);
                        if (!drawn) continue;
                        const float dx = o->pos.x - shown.m[12], dy = o->pos.y - shown.m[13], dz = o->pos.z - shown.m[14];
                        const float d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 < best) {
                            best = d2;
                            dump_drone = o;
                        }
                    }
                }
                wchar_t name[64];
                std::swprintf(name, 64, L"frame_%03d.png", dumped);
                if (opt.frames_png) {
                    if (d3d9 != nullptr) {
                        d3d9->save_png(os::join(opt.frames_dump, name));
                    } else {
                        renderer.save_png(os::join(opt.frames_dump, name));
                    }
                }
                if (dump_tsv != nullptr) {
                    float drone[3] = {0.0f, 0.0f, 0.0f};
                    const DrawItem* d = nullptr;
                    if (dump_drone != nullptr) {
                        if (interp_mode && !interp.empty()) {
                            d = interp.find_owner(dump_drone);
                        } else {
                            for (const DrawItem& it : draws) {
                                if (it.interp.owner == dump_drone && it.mesh >= 0) {
                                    d = &it;
                                    break;
                                }
                            }
                        }
                    }
                    if (d != nullptr) {
                        const float* m = d->palette.empty() ? d->world.data() : d->palette[0].data();
                        drone[0] = m[12];
                        drone[1] = m[13];
                        drone[2] = m[14];
                    }
                    const InterpStats& st = interp.stats();
                    // O rosto mais perto como foi desenhado (os pesos dos morphs, interpolados).
                    std::string face_cols;
                    describe_faces(*items, &shown.m[12], nullptr, &face_cols);
                    std::fprintf(dump_tsv, "%d\t%.5f\t%u\t%.4f\t%d\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.3f\t%.4f\t%.4f\t%.4f\t%u\t%u\t%u\t%u\t%u\t%.4f\t%u\t%u\t%u\t%d\t%s\n",
                                 dumped, psi_clock_vblanks() / 60.0, GameState.ticks - loaded_frame, double(alpha),
                                 interp.cut() ? 1 : 0, double(shown.m[12]), double(shown.m[13]), double(shown.m[14]),
                                 double(XMConvertToDegrees(camera_yaw(shown.m.data()))),
                                 double(XMConvertToDegrees(camera_elevation(shown.m.data()))), double(shown.fov_deg),
                                 double(drone[0]), double(drone[1]), double(drone[2]), st.items, st.matched, st.moving,
                                 st.kept, st.bones, interp_frames != 0 ? interp_ms_total / double(interp_frames) : 0.0,
                                 GameState.draws, View_PortSkySpin(), View_PortLightnings(), scene_cam != nullptr ? 1 : 0,
                                 face_cols.c_str());
                }
                ++dumped;
            }
            const bool dump_done = dumped < 0 || dumped >= opt.frames || opt.frames_dump.empty();
            if (shot_now && !shot_taken) {
                shot_taken = true;
                if (d3d9 != nullptr) {
                    d3d9->save_png(opt.screenshot);
                    if (!opt.d3d9_remix_conf.empty()) d3d9->write_remix_conf(opt.d3d9_remix_conf);
                    // Depuração (NF_D3D9_DUMP): as texturas do Direct3D 11 nos mesmos slots, para comparar.
                    std::wstring dump_dir;
                    if (os::get_env(L"NF_D3D9_DUMP", &dump_dir)) {
                        for (int slot = 0; slot < 4096; ++slot) {
                            int tw = 0, th = 0;
                            std::vector<uint8_t> px;
                            if (!renderer.read_texture(slot, tw, th, px)) continue;
                            wchar_t name[64];
                            std::swprintf(name, 64, L"d11_slot_%04d.png", slot);
                            Renderer::write_png(os::join(dump_dir, name), tw, th, px.data());
                        }
                    }
                } else {
                    renderer.save_png(opt.screenshot);
                }
                char vline[640];
                std::snprintf(vline, sizeof(vline),
                              "video janela %dx%d cena %dx%d escala %d msaa %d cobertura %d/%u filtro %s vsync %d fov %.1f "
                              "hd_tex %d hd_ui %d bordas %d exclusiva %d adaptador %d \"%s\" %s pref %s",
                              renderer.width(), renderer.height(), renderer.scene_width(), renderer.scene_height(),
                              renderer.scene_scale(), renderer.scene_msaa(), renderer.alpha_to_coverage() ? 1 : 0,
                              renderer.alpha_coverage_parts(),
                              nf::texture_filter_name(renderer.texture_filter()), renderer.vsync() ? 1 : 0, double(fov),
                              renderer.using_hd() ? 1 : 0, ui_hd.enabled() ? 1 : 0, ui_hd.anchor_edges() ? 1 : 0,
                              renderer.exclusive_fullscreen() ? 1 : 0, renderer.adapter().index,
                              renderer.adapter().name.c_str(), nf::adapter_kind_name(renderer.adapter()),
                              adapter_note.c_str());
                // O relógio do roteiro de --input na captura (para encaixar passos depois de uma carga).
                char tline[160];
                std::snprintf(tline, sizeof(tline), "\nroteiro t %.2f\nmapa_ambiente partes %u ligado %d",
                              double(GameState.ticks - (front_end ? first_loaded_frame : loaded_frame)) / 60.0,
                              renderer.env_parts(), frame.env_map ? 1 : 0);
                // A roda de armas e a câmera lenta (os ticks rodados desde a partida, o fator em vigor).
                char wline[640];
                g_wheel.describe(wline, sizeof(wline));
                std::string wheel_line = std::string("\n") + wline;
                {
                    char sl[96];
                    std::snprintf(sl, sizeof(sl), " lento %.2f ticks %u alpha %.3f icones_carregados %d",
                                  double(slowmo), slowmo_ticks, double(alpha), modern.wheel_icons());
                    wheel_line += sl;
                }
                std::string face_line;  // o rosto desenhado mais perto (os pesos dos morphs)
                describe_faces(*items, &shown.m[12], &face_line, nullptr);
                write_frame_report(opt.screenshot, levels[level_index], draws.size(), ui_hd.status(),
                                   std::string(vline) + "\n" + pacing_line() + "\n" + face_line + "\n" +
                                       nf::movie_describe() + "\n" + modern.describe() +
                                       (d3d9 != nullptr ? "\n" + d3d9->describe() + " | texturas_d3d11 " +
                                                              std::to_string(gfx.textures())
                                                        : std::string()) +
                                       tline + wheel_line + "\n" + disc_line);
            }
            if (shot_taken && dump_done) {
                if (dump_tsv != nullptr) std::fclose(dump_tsv);
                break;
            }
            if (want_shot) {
                const std::wstring shot = L"nightfire_" + widen(levels[level_index]) + L".png";
                if (d3d9 != nullptr) {
                    d3d9->save_png(shot);
                } else {
                    renderer.save_png(shot);
                }
            }
            const double present_t0 = bench.started ? now_seconds() : 0.0;
            if (d3d9 != nullptr) {
                d3d9->present();
            } else {
                renderer.present();
            }
            if (bench.started) {
                bench.frame_present = (now_seconds() - present_t0) * 1000.0;
                bench.present_ms += bench.frame_present;
            }
            // Só do PC: o limite de quadros ([video] fps_cap, --fps-cap): espera o prazo do próximo quadro (o relógio
            // simulado e as capturas não esperam).
            if (vs.fps_cap > 0 && opt.fixed_video_hz == 0 && !offscreen) {
                const double period = 1.0 / double(vs.fps_cap);
                cap_deadline = next_frame_deadline(cap_deadline, now_seconds(), period);
                wait_until(cap_deadline);
            } else {
                cap_deadline = now_seconds();
            }

            // --bench (só do port): começa --wait depois da carga (da de --shot-level, se veio) e mede cada quadro do fim
            // de um ao fim do outro, até --bench segundos de relógio.
            if (opt.bench > 0.0f) {
                const double bnow = now_seconds();
                const bool ready = frame_count >= 3 &&
                                   (opt.shot_level == 0 ||
                                    (GameState.level_file == opt.shot_level && shot_level_loads >= opt.shot_level_count)) &&
                                   GameState.ticks - loaded_frame >= uint32_t(opt.wait * 60.0f);
                if (!bench.started && ready) {
                    bench.started = true;
                    bench.skip = false;
                    bench.start = bnow;
                    bench.level = GameState.level_file;
                    renderer.gpu_collect(true);
                    renderer.reset_gpu_times();
                    renderer.reset_counters();
                    nf::movie_reset_timing();
                } else if (bench.started) {
                    if (!bench.skip) {
                        bench.frame_ms.push_back((bnow - bench.last_end) * 1000.0);
                        bench.submit_each.push_back(float(bench.frame_submit));
                        bench.present_each.push_back(float(bench.frame_present));
                        bench.tick_each.push_back(float(bench.frame_tick));
                        bench.draw_each.push_back(float(bench.frame_draw));
                    }
                    bench.skip = false;
                }
                bench.last_end = bnow;
                bench.frame_tick = bench.frame_draw = 0.0;
                if (bench.started && bnow - bench.start >= double(opt.bench)) {
                    renderer.gpu_collect(true);
                    const nf::FrameTimeStats st = nf::frame_time_stats(bench.frame_ms);
                    const nf::GpuTimes& g = renderer.gpu_times();
                    const double nf_frames = std::max<double>(1.0, double(st.frames));
                    const double gframes = std::max<double>(1.0, double(g.frames));
                    const nf::AdapterInfo& ad = renderer.adapter();
                    const nf::VideoSettings& lv = video.live();
                    std::string out;
                    char line[1024];
                    std::snprintf(line, sizeof(line),
                                  "bench fase %08x | adaptador %d \"%s\" %s pref %s | %s %dx%d | cena %dx%d escala %d msaa %d "
                                  "cobertura %d/%u filtro %s hd_tex %d (%d/%d) hd_ui %d menus %s interp %d vsync %d limite %s | "
                                  "config %s\n",
                                  bench.level, ad.index, ad.name.c_str(), nf::adapter_kind_name(ad), adapter_note.c_str(),
                                  offscreen ? "fora_da_tela" : "janela", renderer.width(), renderer.height(),
                                  renderer.scene_width(), renderer.scene_height(), renderer.scene_scale(), renderer.scene_msaa(),
                                  renderer.alpha_to_coverage() ? 1 : 0, renderer.alpha_coverage_parts(),
                                  nf::texture_filter_name(renderer.texture_filter()), renderer.using_hd() ? 1 : 0,
                                  gfx.hd_textures(), gfx.textures(), ui_hd.enabled() ? 1 : 0,
                                  nf::menu_style_name(lv.menu_style), lv.interpolation ? 1 : 0, renderer.vsync() ? 1 : 0,
                                  nf::fps_cap_name(lv.fps_cap).c_str(),
                                  opt.config_path.empty() ? "padrao" : narrow(opt.config_path).c_str());
                    out += line;
                    std::snprintf(line, sizeof(line),
                                  "quadros %zu tempo %.2f s fps_medio %.1f | quadro_ms media %.3f p50 %.3f p99 %.3f (1%% low "
                                  "%.1f fps) p99.9 %.3f (0.1%% low %.1f fps) piores_1%% %.3f piores_0.1%% %.3f max %.3f\n",
                                  st.frames, st.total_ms / 1000.0, st.avg_fps, st.avg_ms, st.p50_ms, st.p99_ms, st.low1_fps,
                                  st.p999_ms, st.low01_fps, st.worst1_avg_ms, st.worst01_avg_ms, st.max_ms);
                    out += line;
                    std::string phases;
                    for (int k = 0; k < nf::kGpuPhaseCount; ++k) {
                        char ph[64];
                        std::snprintf(ph, sizeof(ph), " %s %.3f", nf::gpu_phase_name(nf::GpuPhase(k)), g.phase_ms[k] / gframes);
                        phases += ph;
                    }
                    std::snprintf(line, sizeof(line), "gpu_ms media %.3f max %.3f |%s | quadros %llu descartados %llu\n",
                                  g.total_ms / gframes, g.max_ms, phases.c_str(), static_cast<unsigned long long>(g.frames),
                                  static_cast<unsigned long long>(g.disjoint));
                    out += line;
                    const double seconds = std::max(1e-6, st.total_ms / 1000.0);
                    std::snprintf(line, sizeof(line),
                                  "cpu_ms por_tick logica %.3f desenho_do_jogo %.3f (%.1f ticks/s) | por_quadro interpolacao %.3f "
                                  "envio %.3f present %.3f\n",
                                  bench.tick_ms / std::max<double>(1.0, double(bench.ticks)),
                                  bench.draw_ms / std::max<double>(1.0, double(bench.draws_game)), double(bench.ticks) / seconds,
                                  bench.interp_ms / nf_frames, bench.submit_ms / nf_frames, bench.present_ms / nf_frames);
                    out += line;
                    const nf::RenderCounters& rc = renderer.counters();
                    std::snprintf(line, sizeof(line),
                                  "desenhos por_quadro itens_da_lista %.1f itens_desenhados %.1f chamadas %.1f | present tearing %d "
                                  "coberto %llu\n",
                                  double(bench.items) / nf_frames, double(rc.items) / nf_frames, double(rc.draws) / nf_frames,
                                  renderer.tearing() ? 1 : 0, static_cast<unsigned long long>(rc.occluded));
                    out += line;
                    // Os filmes: o custo de CPU do tocador (separar, decodificar no MFT, converter para BGRA, por quadro
                    // de vídeo e por tick) e o pior quadro decodificado.
                    if (nf::movie_stats().started != 0) {
                        const nf::MovieStats& ms = nf::movie_stats();
                        std::snprintf(line, sizeof(line),
                                      "filme %08x cpu_ms por_quadro %.3f | decodificacao media %.3f max %.3f (%llu vezes) | "
                                      "decodificador \"%s\"\n",
                                      MoviePlaying, bench.movie_ms / nf_frames,
                                      ms.decode_calls != 0 ? ms.decode_ms / double(ms.decode_calls) : 0.0, ms.decode_ms_max,
                                      static_cast<unsigned long long>(ms.decode_calls), nf::movie_decoder_name().c_str());
                        out += line;
                    }
                    console_write(out);
                    if (!opt.bench_frames.empty()) {
                        FILE* f = os::open_file(opt.bench_frames, "w");
                        if (f != nullptr) {
                            std::fprintf(f, "quadro\tms\tenvio_ms\tpresent_ms\tlogica_ms\tdesenho_do_jogo_ms\tgpu_ms");
                            for (int k = 0; k < nf::kGpuPhaseCount; ++k) {
                                std::fprintf(f, "\tgpu_%s", nf::gpu_phase_name(nf::GpuPhase(k)));
                            }
                            std::fprintf(f, "\n");
                            const auto& per = renderer.gpu_per_frame();
                            for (size_t i = 0; i < bench.frame_ms.size(); ++i) {
                                std::fprintf(f, "%zu\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f", i, bench.frame_ms[i],
                                             double(bench.submit_each[i]), double(bench.present_each[i]),
                                             double(bench.tick_each[i]), double(bench.draw_each[i]));
                                for (int k = 0; k <= nf::kGpuPhaseCount; ++k) {
                                    std::fprintf(f, "\t%.4f", i < per.size() ? double(per[i][size_t(k)]) : -1.0);
                                }
                                std::fprintf(f, "\n");
                            }
                            std::fclose(f);
                        }
                    }
                    if (!opt.bench_out.empty()) {
                        FILE* f = os::open_file(opt.bench_out, "ab");
                        if (f != nullptr) {
                            std::fwrite(out.data(), 1, out.size(), f);
                            std::fclose(f);
                        }
                    }
                    break;
                }
            }

            fps_time += dt;
            ++fps_frames;
            if (fps_time > 0.5) {
                fps = float(fps_frames / fps_time);
                fps_time = 0;
                fps_frames = 0;
                // O nome da fase em largo num vetor da pilha (o %hs de antes é só do MSVC; um std::wstring aqui seria
                // uma alocação a cada meio segundo a mais na arena da versão de 64 bits).
                wchar_t level_w[32] = {};
                for (size_t i = 0; i < levels[level_index].size() && i + 1 < std::size(level_w); ++i) {
                    level_w[i] = wchar_t(uint8_t(levels[level_index][i]));
                }
                wchar_t title[256];
                std::swprintf(title, 256,
                         L"Nightfire PC · %ls · %zu objetos · %zu triângulos · %dx%d · %.0f fps · lógica %u Hz%ls"
                         L" · HD %ls (%d/%d) · UI HD %ls (F4) · %ls%ls",
                         level_w, draws.size(), gfx.triangles(draws), renderer.width(),
                         renderer.height(), fps, FRAME_RATE_INT,
                         !interp_mode ? L" · sem interpolação" : between_ticks ? L" · interpolado" : L"",
                         renderer.using_hd() ? L"ligado" : L"desligado",
                         gfx.hd_textures(), gfx.textures(), ui_hd.enabled() ? L"ligada" : L"desligada",
                         free_cam ? L"câmera livre (F2)" : L"jogador (F2)",
                         window.mouse_captured() ? L"" : L" · clique para capturar o mouse");
                window.set_title(title);
            }
        }
        ui_hd.finish();
        if (d3d9 != nullptr && !opt.d3d9_remix_conf.empty()) d3d9->write_remix_conf(opt.d3d9_remix_conf);
        gfx.set_mirror(nullptr);
        // Os ganchos apontam para a janela e o renderizador deste escopo.
        video.set_apply(nullptr);
        video.set_store(nullptr);
        MenuPc_SetOptionsHook(nullptr);
        g_modern_app = nullptr;
        nf::audio_out_close();  // o WAV de --audio-dump com os tamanhos certos
        return 0;
    } catch (const std::exception& e) {
        nf::audio_out_close();
        os::show_error_narrow("Nightfire PC: erro", e.what());
        return 1;
    }
}

