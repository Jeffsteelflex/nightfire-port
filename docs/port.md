# Executável nativo (`nightfire.exe`)

Primeira peça do port: um programa Windows que lê `ACTION.ELF` e `FILES.BIN` da cópia do
usuário (a imagem `.iso` do disco, o disco num leitor de DVD, os arquivos extraídos numa pasta ou a cópia pessoal dentro
do próprio executável: ver "Instalação fácil") e roda as fases com Direct3D 11: o jogador anda pela fase com mouse e teclado, com o
movimento e a colisão do próprio jogo. Não usa emulação nem arquivos convertidos.

## Compilar e rodar

```
build.bat                         :: Release em build\Release\nightfire.exe (32 bits, a versão de referência)
build.bat Release x64             :: a versão de 64 bits em build\x64\Release\nightfire.exe (ver "64 bits")
build\Release\nightfire.exe --level 07000001
```

Sem `--disc`, o executável procura o disco sozinho (a ordem está em "Instalação fácil": a cópia pessoal, a pasta dele,
a pasta atual e as de cima, o `[game] disc` do `nightfire.ini`, os leitores de DVD) e, sem nenhum que sirva, mostra a
tela de configuração. Quem compila tem os arquivos extraídos na raiz do repositório, que a procura acha a partir de
`build\Release`; `--disc` (ou o nome antigo `--data`) indica outro.

| Opção | Efeito |
|-------|--------|
| `--disc <caminho>` | o disco do jogo: uma imagem `.iso`, a pasta com os arquivos extraídos, um leitor (`D:`) ou um executável com a cópia pessoal (`--data` é o nome antigo). Só a versão americana (SLUS-20579) serve; ver "Instalação fácil" |
| `--setup` | mostra a tela de configuração mesmo com um disco que sirva (trocar o disco, fazer a cópia pessoal) |
| `--setup-pick <caminho>`, `--setup-input "<seq>"` | capturas da tela de configuração: a resposta da janela de escolha de arquivo e o roteiro (como `--input`, com `cmouse=x,y`; o relógio conta da tela) |
| `--pack-disc <caminho>` | faz a cópia pessoal em um arquivo só e sai (sem janela; o andamento no console); com `--out <arquivo.exe>` (padrão: `<executável>-personal.exe` ao lado dele) e `--pack-no-movies` (sem os filmes, uns 850 MB a menos) |
| `--level <hash>` | começa direto nessa fase; sem ele, o menu principal do jogo (07000048) |
| `--save-dir <pasta>` | a pasta do cartão de memória (padrão `saves\` ao lado do executável); com `--screenshot` e sem ele, o cartão fica em memória |
| `--width`, `--height` | tamanho da janela (padrão 1600x900) |
| `--fullscreen` | tela cheia sem bordas, na resolução nativa do monitor (o mesmo que `--window-mode borderless`) |
| `--window-mode <m>` | `windowed` (janela com bordas, do tamanho de `--width`/`--height`), `borderless` (sem bordas, do tamanho do monitor) ou `exclusive` (tela cheia exclusiva do DXGI, que troca o modo do monitor para `--width` x `--height`) |
| `--render-scale <pct>` | a cena 3D em `pct`% do tamanho da janela (50 a 200; acima de 100 é supersampling); o 2D fica na resolução da janela |
| `--msaa <n>` | MSAA da cena 3D com 2, 4 ou 8 amostras (1: desligado); com ele, os recortes do teste de alfa (folhas, plantas) vão com o alfa para a cobertura (alpha-to-coverage, abaixo) |
| `--no-alpha-coverage` | depuração: com MSAA, o teste de alfa descarta o pixel inteiro, como sem MSAA (para comparar) |
| `--filter <f>` | filtro das texturas: `bilinear`, `trilinear`, `aniso2`, `aniso4`, `aniso8` (padrão) ou `aniso16` |
| `--lang-pc pt\|en` | idioma dos textos só do port (as opções do PC no menu); padrão `en` |
| `--config <ini>` | o arquivo de opções (padrão: `nightfire.ini` ao lado do executável; ver "Opções do PC no menu") |
| `--fov <graus>` | campo de visão vertical (padrão 60); o horizontal cresce com a largura (Hor+) |
| `--sensitivity <x>` | radianos por contagem do mouse (padrão 0.0022) |
| `--novsync` | sem sincronismo vertical |
| `--fps-cap <n>\|off` | limite de quadros por segundo (`off` ou `unlimited`: sem limite; também `[video] fps_cap`; ver "Interpolação entre ticks") |
| `--no-interp` | sem a interpolação entre ticks: o `Game_Draw` roda a cada quadro de vídeo e cada quadro mostra o último tick, como antes (`[video] interpolation = off`; `--interp` a liga) |
| `--no-instant-mouse` | sem o olhar do mouse sem atraso (`[video] instant_mouse_look = off`) |
| `--fixed-video-hz <n>` | capturas: um monitor simulado de `n` Hz (cada quadro de vídeo avança 60 / `n` VBlanks no relógio do jogo), para quadros entre ticks determinísticos |
| `--frames-dump <pasta>` com `--frames <n>` | capturas: a partir do quadro da captura, `n` quadros seguidos (`frame_000.png`... e uma linha cada em `frames.tsv`: relógio, tick, alpha, corte, a câmera desenhada, o personagem mais perto como foi desenhado, o custo); `--frames-no-png` grava só o `frames.tsv` |
| `--interp-alpha <a>` | depuração: o alpha da interpolação fixo (0 o tick anterior, 1 o atual) |
| `--only-2d` | depuração: desenha só o 2D (HUD, menus, legendas) sobre o fundo, para comparar o 2D entre caminhos |
| `--renderer d3d9ff` | o protótipo de Direct3D 9 de função fixa para o RTX Remix (ver "Caminho Direct3D 9 para o RTX Remix" e `docs/remix.md`); o padrão é `d3d11`. Com ele: `--d3d9-no-lights`, `--d3d9-viewmodel <z>`, `--d3d9-remix-conf <arquivo>` |
| `--screenshot <png>` | renderiza 3 quadros, salva a imagem e sai (ignora mouse e teclado); desenha fora da tela, numa textura do tamanho de `--width`/`--height`, então a captura pode ser maior que a área de trabalho (2560x1440, 3840x2160) |
| `--cam x,y,z,tx,ty,tz` | começa na câmera livre, com essa posição e alvo |
| `--drawall` | começa com `switch_ForceDrawAll` ligado (tecla P) |
| `--use` | usa o que está à frente uma vez no começo (para capturas) |
| `--wait <segundos>` | com `--screenshot`, captura depois desse tempo de jogo (conta os ticks desde a carga da fase, também com o jogo pausado por um menu) |
| `--shot-level <hash>[:n]` | com `--screenshot`, só captura nesse pacote (o `--wait` conta da carga dele), por exemplo o menu principal `07000048` depois de uma missão; com `:n`, só na n-ésima carga dele (sem `--level`, `07000048:2` é a volta ao menu principal depois da missão que o roteiro escolheu) |
| `--win <segundos>` | depuração: liga o canal 100 (todos os objetivos cumpridos) esse tempo depois da carga da primeira fase que não é o menu principal; a missão termina e o menu principal mostra o resultado |
| `--input "<seq>"` | entrada roteirizada para capturas: trechos `teclas:segundos` (teclas unidas por `+`: letras, dígitos, `space`, `left`, `right`, `up`, `down`, `ctrl`, `lmb`, `rmb`, `enter`, `back`, `tab` e os nomes do `[bindings]`, como `escape`, `delete`, `f5`; `none` é nenhuma), por exemplo `"left:0.5 W:2 E:0.1"`; um controle simulado com `pad_a`, `pad_rt`, `pad_start`... e os analógicos `pad_lx=v`, `pad_ly=v`, `pad_rx=v`, `pad_ry=v` (-1 a 1, y para cima), por exemplo `"pad_ly=1:2 pad_rt:0.5"`; o mouse dos menus com `mouse=x,y` (o cursor nesse ponto da tela do PS2, 512 x 448, a partir do trecho), `wheel=n` (n dentes no começo do trecho), `lmb`, `rmb` e `mouse4` (ver "Mouse nos menus"); o mouse olhando com `mlook=dx,dy` (contagens por segundo durante o trecho, somadas a cada quadro de vídeo); vale também com `--cam`, para filmar o jogador de fora |
| `--open-menu-page <hex>` | depuração: abre a pausa e mostra essa página meio segundo depois da carga (`40000060` as opções do PC) |
| `--menu-tab <aba>` | com `--open-menu-page 40000060`: a aba (`video`, `graphics`, `interface`, `audio`, `controls`, `keys`; no estilo Modern também `controller`) |
| `--menu-style modern\|original` | o estilo dos menus do PC (padrão `modern`; ver "Menus no estilo Modern"); também `[ui] menu_style` |
| `--at x,y,z,giro` | põe o jogador de pé nesse ponto, virado para o giro (radianos); o `nf_loader_check` mostra pontos diante de portas em `porta_usada=` |
| `--textures <pasta>` | pacote de texturas HD no formato do PCSX2 (padrão: `textures\SLUS-20579\replacements` na pasta do disco (a pasta dos arquivos, a da imagem `.iso`), senão ao lado do executável ou nas pastas de cima dele, senão na pasta atual) |
| `--no-hd` | começa com as texturas originais (tecla T alterna) |
| `--no-lights` | depuração: sem as luzes da VU1 (as rotinas de luz viram as sem luz; o tweak continua) |
| `--no-fog` | depuração: sem a passada de névoa (o alfa dos transparentes ainda cai com a distância, como no `psi_draw`) |
| `--debug-shading` | começa com o sombreamento de depuração (tecla V alterna) |
| `--no-subtitles` | sem legendas (a opção do menu, `DrawInfo[2]`; o port começa com elas ligadas, o PS2 americano desligadas) |
| `--no-audio` | sem saída de som (o jogo e o driver rodam igual; o padrão com `--screenshot`); ver "Som" |
| `--audio-dump <wav>` | grava o som num WAV estéreo de 48 kHz em vez de tocá-lo (também nas capturas: o som de uma captura é sempre o mesmo) |
| `--movies` / `--no-movies` | liga ou desliga os filmes (padrão: ligados, `[video] movies`; as capturas e o `--bench` começam sem eles); ver "Filmes" |
| `--movie-frame <n>` | capturas: liga os filmes e captura no quadro n do filme que tocar (com `--movie <hash>`, só desse) |
| `--intro-movie` | com `--level`: a fase pedida com o filme de abertura (07000001 -> 07100001), como o mapa das missões a pede |
| `--hd-ui` | começa com a camada 2D em alta definição (tecla F4 alterna; ver "Camada 2D em alta definição") |
| `--hd-ui-dir <pasta>` | pasta com o `map.json`, as substitutas e `fonts/` (padrão: a `hd_ui` do repositório, achada a partir do executável, ou a da pasta dos dados ou da atual) |
| `--hd-ui-anchor centre\|edges` | âncora do HUD na camada HD: `centre` como o original, `edges` presa às bordas de uma janela larga (padrão: o `anchor` do `map.json`) |
| `--ui-inventory <pasta>` | inventário das imagens 2D desenhadas: `inventory.tsv` (somado ao que já houver na pasta) e um PNG de cada recorte e de cada textura (ex.: `out/ui_crops`) |
| `--prompts auto\|ps2\|keyboard\|xbox` | símbolos de botão da camada HD: o último aparelho usado (padrão), os do PS2, as teclas ligadas ou os botões do Xbox (também `[ui] prompts` no `nightfire.ini`) |
| `--adapter <a>` | o adaptador de vídeo: `auto` (padrão: o do sistema, como antes), `integrated`, `discrete`, o índice do `--list-adapters` ou um trecho do nome (`radeon`, `"rtx 5090"`); também `[video] adapter` e a linha **Graphics adapter** de VIDEO (ver "Adaptador de vídeo e benchmark") |
| `--list-adapters` | escreve os adaptadores (índice, nome, fabricante, memória dedicada, integrado ou dedicado, com monitor ou não, as ordens de consumo e de desempenho do Windows) e o que a preferência escolheria, e sai |
| `--bench <s>` | benchmark: a fase sem limite de quadros nem VSync, medida por `s` segundos de relógio depois do `--wait` (padrão 1 s); escreve no console (e em `--bench-out <txt>`, acrescentado) os quadros, o fps médio, o tempo de quadro (média, p50, 1% e 0,1% low, máximo), o tempo de GPU por trecho, os custos de CPU, os desenhos e as opções. Com `--offscreen`, numa textura sem `Present` (o custo da própria GPU); `--bench-frames <tsv>` grava cada quadro; `--bench-vsync` mantém o VSync e o limite das opções |

Com `--screenshot` também sai um `.txt` ao lado da imagem: a sala da câmera, as salas visíveis
(índice na lista do mundo, tipo, se deixam ver o céu), quantos desenhos o jogo pediu e a posição,
o giro e o subestado do jogador. A linha `2d` diz o idioma, as mensagens aceitas por tipo, os sprites e
quantos elementos 2D o quadro desenhou; cada texto desenhado sai numa linha `texto2d x y fonte "texto"`
(posição na tela do PS2) e cada imagem numa `imagem2d`. A primeira linha, `ui_hd`, diz se a camada 2D em
alta definição está ligada, a pasta, a âncora, quantas substitutas o `map.json` tem e as fontes escolhidas.
Antes dela, a linha `video` diz as opções de vídeo em vigor (tamanho da janela e da cena, escala, MSAA,
filtro, VSync, campo de visão, texturas e interface HD, âncora, tela cheia exclusiva e o adaptador de vídeo: índice,
nome, `integrated`/`discrete` e a preferência, `pref auto` ou o que veio de `--adapter`) e a linha
`pc_opcoes` a página de opções do PC (página atual, linha escolhida, a caixa de confirmação, quantas
aplicações foram gravadas, mantidas e desfeitas, e os valores pendentes e em vigor). A linha `ritmo` é a da
interpolação entre ticks (ver lá): ligada, o alpha, se o tick é um corte, os itens do quadro, os com par, os que
se movem, os que saltaram e os mantidos do tick anterior, os ossos interpolados, o custo (`custo_ms` por quadro de
vídeo, `tick_ms` por tick), os quadros por tick, o limite, o mouse sem atraso, a câmera desenhada (posição, giro,
elevação, campo de visão), os ticks desde a carga, os `Game_Draw` (`desenhos`, `GameState.draws`), o céu que gira
(`ceu_gira`), os relâmpagos começados / os elementos do céu com relâmpago e o `--fixed-video-hz`.

Controles: o clique captura o mouse; abrir um menu (Esc, a pausa) e perder o foco (Alt+Tab) o soltam. Na câmera
livre o Esc solta o mouse; jogando, se nenhum menu abrir meio segundo depois do Esc (uma cena, a morte, um
carregamento), ele também solta: o cursor nunca fica preso.

- Jogando (o Bond, câmera do jogo): o mouse olha; W/S anda; A/D anda de lado; as setas viram e
  olham como o analógico direito; Espaço pula; C ou Ctrl agacha; botão esquerdo atira e o direito mira
  (segurando); E usa (portas, interruptores) ou recarrega, como o botão do PS2; R recarrega; o botão do meio ou X
  troca a variante ou o modo de disparo; a roda troca de arma (para baixo a próxima, para cima a anterior; Z e Q
  também) e, mirando com zoom, dá zoom; 1 a 5 escolhem o primeiro ao quinto aparelho e G o próximo; N é a visão
  (noturna, raio X: o select); Enter ou Esc o Start (a pausa). São os padrões da tabela de teclas (ver a tabela em
  "Controles (teclado, mouse e controle)"), que OPTIONS > KEY BINDINGS (Modern) ou OPTIONS > KEYS (Original) muda; um
  controle XInput também joga, com os estilos de botões do jogo. Os inimigos ferem o jogador; morto, a cortina de
  sangue do HUD desce e, quando cobre a tela ou com Enter, Esc ou Espaço, a missão fracassa e o menu "Mission Failed"
  (REPLAY/QUIT) abre sobre o jogo pausado; o mesmo acontece quando um objetivo ou um alarme faz a missão
  falhar. Com a missão cumprida, o menu principal mostra o resultado e, ao continuar, o mapa das missões,
  com a seguinte liberada. O HUD do jogo aparece (vida e
  colete, munição, mira, mensagens, setas de dano, cortina de sangue); o relatório de `--screenshot`
  resume o estado dele, da missão e dos menus (linhas `hud`, `missao` e `menus`).
- Nos menus (os do jogo, ver `engine.md`, "Menus"): Enter é o Start (abre e fecha a pausa; nos menus de
  fim de missão e no principal também escolhe), Espaço o X (escolhe), Tab o círculo, Esc ou Backspace o
  triângulo (volta; no topo da pausa, fecha a pausa, só do PC), setas ou WASD o direcional, E o quadrado (o dossiê no mapa das missões e no resultado,
  "Create" nos codinomes: a mesma tecla da aba DOSSIER do mapa Modern; no jogo o E continua "usar"). Um menu aberto recebe o
  teclado também na câmera livre.
- F2 alterna para a câmera livre: WASD move; Q e E descem e sobem; Shift acelera; Ctrl
  desacelera; a roda ajusta a velocidade; Espaço (sem menu aberto) abre ou fecha a porta à frente. Ela começa onde o
  jogador está olhando e mostra o corpo dele, animado (o jogo o esconde da câmera do jogador). Fases
  sem ponto de partida só têm a câmera livre.
- Sempre: PgUp e PgDn trocam de fase; V alterna o sombreamento de depuração (luz direcional fixa no
  lugar das cores dos vértices e das luzes do jogo, como antes deste port ter a iluminação); F3 alterna
  wireframe; H mostra
  gatilhos e marcadores; P liga o `switch_ForceDrawAll` do jogo (todas as salas, até o limite de 64
  da lista de visíveis, sem os portais); T alterna as texturas HD; F4 alterna a camada 2D em alta
  definição; F12 salva uma captura;
  Alt+Enter alterna a tela cheia.

O jogador nasce no ponto de partida da fase (`Player_Start`). A câmera 0 do jogo mostra o que a
visibilidade por portais do original deixa ver, então fora do mapa quase nada é desenhado.

O movimento é o do PS2: andar para a frente chega a 0,1 m por tick (6 m/s), para trás e de lado
são mais lentos, a diagonal do teclado vale o que valeria a do analógico (os dois eixos a 0,707),
e virar pelas setas acelera de 2,4 a 4,8 rad/s segurando. O mouse é uma divergência do PC: gira a
visão direto, em radianos por contagem (`--sensitivity`), sem as curvas de aceleração do analógico.
A lógica (e a câmera do jogador) roda a 60 Hz; acima disso a imagem fica entre os dois últimos ticks (ver
"Interpolação entre ticks"), e o mouse gira a visão na hora.

## Instalação fácil

Para quem só quer jogar: baixar um zip, apontar a imagem do disco (ou pôr o disco no leitor) e jogar, sem compilar nem
extrair nada. O conteúdo do jogo nunca é distribuído: cada jogador usa a sua cópia do disco americano (SLUS-20579).

### As fontes do disco (`src/platform/disc_source.h`)

Toda leitura dos arquivos do disco passa pela fonte do disco (`nf::DiscSource`): o `ACTION.ELF` na partida
(`game/boot.h`: o `FileList` e os construtores estáticos), o `FILES.BIN` (`psi_file`, `FilesBin`), a pasta `PS2\` do
driver de som (`sfx_iop_set_disc`), os filmes `MOVIES\30_FPS` (`movie_set_disc`, o `PssFile` lê um `DiscFile`) e, nas
ferramentas das missões de carro, o `DRIVING\` e o `DRIVING.ELF` (`drv::Bigf`, `nf_driving --data`, `nf_driving_check`).
Os caminhos são os do disco, com `/` e sem diferenciar maiúsculas (`PS2/ENGLISH/SB_0/SB_0.SFX`). Um arquivo aberto
(`nf::DiscFile`) é uma faixa de um arquivo do PC com o identificador do Windows dele (cada abertura tem o seu cursor).
Quatro fontes, a mesma interface:

| Fonte | O que é | Como os arquivos são achados |
|-------|---------|------------------------------|
| pasta | os arquivos extraídos (a raiz do repositório de quem compila, ou ao lado do executável) | o arquivo da pasta |
| imagem | o `.iso` do DVD do PS2 | o ISO 9660 da imagem: o descritor primário no setor 16, os diretórios (os registros não passam de um setor para o outro: um byte 0 pula para o próximo), os nomes com `;1` e o ponto final dos nomes sem extensão; cada arquivo é uma faixa contínua da imagem (um arquivo em várias partes só se elas forem contínuas; intercalados são recusados). O UDF que os DVDs também têm não é lido |
| unidade | o disco num leitor de DVD (ou a imagem montada pelo Windows) | uma pasta na raiz da letra (`D:\`), sem as caixas "não há disco" do Windows |
| cópia | a cópia pessoal (abaixo) | o índice no fim do próprio executável |

A versão vem do `SYSTEM.CNF`: `BOOT2 = cdrom0:\SLUS_205.79;1` dá `SLUS-20579`. O jogo diz o que espera (`DiscTitle`,
`nf::nightfire_disc()` em `game/boot.h`: o código, os arquivos sem os quais ele não roda, o que entra na cópia) e
`check_disc` responde: serve, não existe, não dá para ler (não é ISO 9660), não é um disco de PS2 (sem `BOOT2`), é de outro
jogo (outro código, sem os arquivos do jogo), é outra versão do Nightfire (outro código com `ACTION.ELF` e `FILES.BIN`: os
PAL, `SLES-...`, ainda não são suportados) ou falta um arquivo. Uma pasta sem `SYSTEM.CNF` mas com `ACTION.ELF` e
`FILES.BIN` (o jeito antigo de extrair) serve, com a versão não conferida. Nada disso é do Nightfire: a "007 Anthology"
reaproveita a fonte com outro `DiscTitle`.

A procura (`find_disc`), quando a linha de comando não diz: (1) a cópia pessoal no próprio executável; (2) a pasta do
executável, com os arquivos ou com uma imagem `.iso` (a primeira que sirva, por nome); (3) a pasta atual e as três de
cima do executável (a raiz do repositório a partir de `build\Release` e `build\x64\Release`: quem compila); (4) o `[game]
disc` do `nightfire.ini`; (5) os leitores de CD/DVD (`D:\` com um `SYSTEM.CNF`). Os recusados no caminho (outra versão,
uma imagem que sumiu) ficam anotados para a tela de configuração. As pastas da procura só contam com `FILES.BIN` ou
`SYSTEM.CNF` dentro. `--disc` vale sozinho. Nas capturas sem `--config` o `nightfire.ini` não é lido, então o `[game]
disc` também não: uma captura não depende das opções do jogador.

O pacote de texturas HD do jogador (formato do PCSX2) fica ao lado do disco: `textures\SLUS-20579\replacements` na pasta
dele (a da imagem, para um `.iso`), senão na pasta do executável e nas de cima, senão na atual; com o disco num leitor de
DVD, ao lado do executável (ou `--textures`).

### A tela de configuração (`src/ui/modern_setup.h`, `src/render/setup_app.h`)

Sem um disco que sirva (ou com `--setup`), o `nightfire.exe` abre a janela e mostra a tela de configuração antes do
jogo, no estilo Modern (o kit, docs/design.md "As telas"), sem nada do jogo (não há disco ainda): "Select your 007:
Nightfire disc image (PS2, USA)", SELECT DISC IMAGE (a janela de escolha de arquivo do Windows, `IFileOpenDialog` numa
thread STA própria; o laço continua desenhando), USE THE DISC IN DRIVE X: (um por leitor com um disco de PS2, procurados
de novo a cada 2 s: um disco que entra aparece sozinho; o de outra versão com o cadeado e o motivo), PLAY, MAKE A
SINGLE-FILE COPY e QUIT. Um disco escolhido é conferido (`probe_disc`); servindo, vai para o `[game] disc` do
`nightfire.ini` e o foco vai para PLAY; senão, a caixa diz por quê (os textos em `PcText`, inglês e português por `[ui]
language_pc`). PLAY segue a partida normal (o menu principal do jogo); da próxima vez a procura acha o disco pelo `.ini`.
Teclado, mouse e controle como nas outras telas Modern (`NavState`).

Capturas: `--screenshot` desenha a tela fora da janela no relógio fixo de 60 quadros por segundo; `--setup-pick
<caminho>` responde a janela de escolha e `--setup-input` é o roteiro; se o `--wait` passa com a tela aberta, grava a
imagem e o `.txt` (`configuracao foco linhas pronto problema copia leitores dialogo escolhendo mouse disco`, o disco
escolhido, os recusados e os leitores) e sai. Ex.: num lugar sem disco,
`nightfire.exe --config out/s.ini --setup-pick "<iso>" --setup-input "none:0.5 enter:0.1 none:0.5 enter:0.1" --input
"none:4.5 enter:0.1" --wait 7 --screenshot out/shots/s.png` escolhe a imagem, a grava em `out/s.ini`, joga e captura o
menu principal (igual, pixel a pixel, ao de antes).

### A cópia pessoal em um arquivo só (`src/platform/disc_pack.h`)

Para o próprio jogador: `nightfire.exe --pack-disc <iso|pasta|leitor> [--out <arquivo.exe>] [--pack-no-movies]`, ou MAKE
A SINGLE-FILE COPY na tela de configuração (a thread da cópia, a barra de andamento e Cancel na caixa). O resultado é uma
cópia do executável em uso com os arquivos do disco depois da imagem PE (o "overlay", que o carregador do Windows não
mapeia) e um rodapé de 64 bytes no fim (o tamanho do programa, onde está o índice, o formato, se tem os filmes, o código do
disco e a marca `NFPC-DISC-PACK-1`). Entra só o que o port usa: `ACTION.ELF`, `SYSTEM.CNF`, `FILES.BIN`, `PS2\` e, se não
for `--pack-no-movies`, `MOVIES\` (o `DRIVING\` fica de fora até as missões de carro rodarem), cada arquivo alinhado a
2048, e a pasta `hd_ui` do port em `PORT/HD_UI/` (uma cópia sozinha numa pasta qualquer a tira uma vez para
`%LOCALAPPDATA%\Nightfire PC\hd_ui-<marca>`). Na partida o executável lê o próprio fim: com o rodapé, ele é o disco (o
passo 1 da procura). Uma cópia feita por uma cópia leva só o programa (sem aninhar a de antes).

O arquivo contém o jogo do usuário: é só dele e não pode ser compartilhado (a tela e o `--pack-disc` dizem isso). Ele vai
só para onde o usuário mandar, por padrão ao lado do executável em uso (`nightfire-personal.exe`, `nightfire64-personal.exe`),
e nunca para dentro de uma árvore de trabalho do git (`inside_git_tree`: uma pasta acima com `.git`; o `.gitignore` já
ignora `*.exe`, mas a regra vale para qualquer nome); a escrita vai para um `.part` trocado pelo nome final no fim.

Tamanho e o Windows (12/10/2026, esta máquina): com os filmes, 1 909 214 396 bytes a de 32 bits e 1 910 004 924 a de 64
(1,8 GB: `FILES.BIN` 764 MB, `MOVIES\` 848 MB, `PS2\` 290 MB, o `hd_ui` 2 MB); sem eles, ~1,06 GB. O Windows 11 roda as
duas nesse tamanho, também sozinhas numa pasta qualquer (o `hd_ui` tirado para a pasta do usuário): a primeira partida
de uma cópia nova demorou 7 a 14 s (o Defender lê o arquivo inteiro), as seguintes 1,5 s, como o executável comum. A
cópia feita pela tela de configuração, conferida com o `nf_disc_check` contra a imagem: os 137 arquivos do disco iguais. A
versão de 32 bits precisa do runtime do Visual C++ (`msvcp140.dll`, `vcruntime140.dll`: no zip ao lado dele, ou o
instalado no sistema); a de 64 bits o tem dentro (`/MT`) e roda sozinha em qualquer pasta.

### O zip de lançamento (`tools/make_release.py`)

`python tools/make_release.py [--version X.Y]` monta, das saídas de `build\Release` e `build\x64\Release`, a pasta
`Nightfire PC` e o zip dela em `out/release/` da cópia principal do repositório (git-ignorado; nada é publicado):
`nightfire.exe` + `nightfire.pdb` (32 bits, a referência e o padrão), `nightfire64.exe` + `nightfire64.pdb` (64 bits,
marcado como experimental no `README.txt`; o CMake grava o PDB dela com esse nome), `msvcp140.dll` e `vcruntime140.dll`
(o runtime do Visual C++ x86 do Build Tools, distribuível), a pasta `hd_ui` (só os arquivos que o git conhece: as fontes
Rajdhani com o `OFL.txt`, os SVG, o `map.json`, o `design/tokens.json`) e os textos de `tools/release/` (`README.txt`:
como jogar; `LICENSES.txt`: as peças de terceiros). Os executáveis só guardam o nome do PDB (`/PDBALTPATH`), e o relatório
de falhas procura o PDB ao lado do executável. O `build.bat` põe os temporários do compilador e do linker em
`build\<config>\tmp`: o PDB guarda o caminho deles, e o `%TEMP%` do Windows traz o nome do usuário. O script recusa qualquer
coisa do jogo (nomes e pastas do disco, arquivos grandes que não sejam executáveis ou PDB) e procura no conteúdo o nome e o
e-mail da conta do git e o nome do usuário do Windows, sem nunca escrevê-los.
O que cada executável precisa para rodar foi visto numa pasta limpa fora do repositório: o `hd_ui` ao lado dele, o runtime
para o de 32 bits, mais nada (os shaders são compilados do texto que está no executável).

### Conferências

- `nf_disc_check <A> [B]` (`tests/disc_check.cpp`): a versão e a lista de uma fonte; com duas, cada arquivo de A procurado
  em B e comparado byte a byte. A imagem do usuário contra os arquivos extraídos: 177 arquivos iguais (2 438 104 348 bytes);
  contra a imagem montada (`Mount-DiskImage`, uma letra como um DVD): iguais.
- `nf_level_check`, `nf_loader_check` e `nf_driving_check` aceitam a imagem (ou o leitor) no lugar da pasta, com a mesma
  saída.
- As capturas da imagem, do leitor e da cópia pessoal são iguais, pixel a pixel, às da pasta (07000012 a 2 s e 9 s, 07000001
  a 11,7 s, o menu principal Modern e Original, um quadro de filme e a abertura de uma missão; os WAV também).

## Arquitetura

| Pasta | Conteúdo | Substitui no PS2 |
|-------|----------|------------------|
| `src/core` | leitura binária e de arquivos | |
| `src/engine` | código decompilado do ACTION.ELF: matemática, tabela de hashcodes, GameFlow, memória, carregador (`ResetMap_Load`, `LoaderLoad`, `parsemap_*`) | as mesmas funções |
| `src/data` | ELF, `FILES.BIN`, pacotes de fase, itens de mapa; decodificação de texturas e da geometria do bloco 0x2d; interpretador MIPS (`mips_interp`) para os construtores estáticos do ELF | leitura de formato usada pelo backend e pelas ferramentas; o `__main` do crt0 |
| `src/game` | `boot` (índice `FileList`, tabelas dos construtores estáticos), `static_init` (`weapon_data`, `ammo_data`), `LevelScene` (leitor independente, usado só pelo `nf_level_check`), tipos de objeto, câmera inicial | |
| `src/render` | Direct3D 11, malhas rígidas e com skinning (skinning na GPU), estado do GS por parte de malha, iluminação da VU1 por vértice (tweak, luzes, mapa de ambiente), passada de névoa, sprites de partículas, triângulos soltos (estilhaços), o 2D na tela (sprites e texto); `runtime_gfx` liga as estruturas do jogo ao renderizador; `ui_hd*`, a camada 2D em alta definição (Direct2D, DirectWrite, WIC); `remix_d3d9`, o protótipo de Direct3D 9 de função fixa para o RTX Remix (`--renderer d3d9ff`) | GS, VU1 (`DrawThisBox`, microprogramas), VU0 do skinning (`SkinIt`) |
| `src/platform` | janela Win32, Raw Input, tela cheia; `psi*` (relógio, heap, arquivos, recursos gráficos, registro do desenho de cada quadro, inclusive `psiDrawSkinObjectMatrix`, `psiDrawParticleList`, as luzes, a névoa e os esmaecimentos (`psiFadeView`); a tela de carga (`psi_loading`); os estilhaços de vidro, `psi_shard`; o registro 2D, `psi_2d`); `vu1` (as contas das rotinas da VU1 e da névoa na CPU) | `libpad`, VBlank, saída NTSC, `malloc` do heap, CD, VRAM, cadeias de DMA do quadro |

Decisões:

- Matrizes em vetor-linha (`v' = v * M`), como o motor; os shaders usam `row_major`.
  As matrizes do bloco `0x1A` entram sem conversão.
- Profundidade invertida com `D32_FLOAT`: o céu fica a quilômetros e o primeiro plano a centímetros.
- Faces dos dois lados, como o jogo desenha no GS.
- Mistura, teste de alfa e Z por parte de malha, pelo estado do GS que a cadeia de cada malha
  escreve (pares A+D) e, no que ela não escreve, pelo do desenho (`psiSetUpColourBlend`); ver
  "Estado do GS, tweak e partículas". Os desenhos vêm na ordem do `Game_Draw` (a lista transparente
  já vem de trás para a frente).
- Texturas com mipmaps e filtro anisotrópico 8x por padrão (a página de opções troca); o PS2 usava
  bilinear sem mipmaps nesses mapas.
- Skinning na GPU: a malha fica na pose de repouso e o vertex shader mistura dois ossos da paleta de
  cada desenho, no lugar do `SkinIt` da VU0 (ver "Modelos com skinning" abaixo).
- Mouse por Raw Input: delta direto do sensor, sem a aceleração do Windows.
- Gatilhos, volumes de chuva, pontos de spawn e nós de IA têm malha no disco, mas o jogo não
  os desenha. A classificação por tipo está em `classify_object` (`src/game/level_scene.cpp`).

## 64 bits

O port compila em 32 bits (x86, a versão de sempre, a padrão e a referência de todas as conferências) e em 64 bits
(x64). A de 64 bits é o primeiro passo para o macOS, que só roda programas de 64 bits; no Windows a de 32 bits continua
a padrão e não mudou em nada (todas as conferências e capturas idênticas às do `main`).

```
build.bat Release          :: x86 em build\Release (vcvarsamd64_x86, /LARGEADDRESSAWARE), como antes
build.bat Release x64      :: x64 em build\x64\Release (vcvars64, -DNF_ARCH64=ON)
build\x64\Release\nf_tests.exe
build\x64\Release\nf_loader_check.exe .
```

Opções do CMake: `NF_ARCH64` (desligada: x86) e, só em x64, `NF_P32_CHECK` (ligada). Em x64 o C runtime é estático
(`/MT`, para o `operator new` do port valer também dentro do STL), a troca de pilha é um arquivo MASM e truncar um
ponteiro é erro de compilação (`/we4311 /we4312 /we4302`).

### Ponteiros de 4 bytes

As estruturas do jogo têm o layout do PS2 (cada campo de ponteiro com 4 bytes; os tamanhos e deslocamentos conferidos
com `static_assert`) e o código decompilado lê e escreve ponteiros dentro dos blocos carregados do disco. Em vez de
traduzir tudo isso, a versão de 64 bits guarda em cada campo de ponteiro o deslocamento do objeto a partir do começo da
imagem do executável (`__ImageBase`), e garante que tudo o que o jogo pode apontar fique numa janela de 4 GB a partir
dali (`src/platform/ptr32.h`, `src/platform/arena.h`, comentários do cabeçalho):

- `P32<T>`, o tipo de um campo de ponteiro numa estrutura do jogo: em x86 é `T*` (o código compila exatamente como
  antes); em x64 uma classe de 4 bytes (0 = nulo) que converte de e para `T*` sozinha (`->`, `*`, `[]`, `++`, `+=`,
  comparações). `F32<R(A...)>` faz o mesmo com um ponteiro de função: o código fica na imagem, então o deslocamento
  basta (dispensa um registro de funções; as tabelas copiadas do ELF continuam resolvidas por `Engine_ResolveCode`).
- A janela contém a imagem (o código, as globais, as strings literais) e a arena: a maior região livre da janela logo
  depois da imagem, reservada sem comprometer. Dela saem o heap do jogo (`psiMem_Init`), todo `new`/`delete` do programa
  (o `operator new` é trocado em x64: `std::vector`, `std::string` e os buffers que o port entrega ao jogo já caem na
  janela) e a pilha onde o jogo roda (`nf::run_main` envolve o `main` de cada executável; o código decompilado guarda
  endereços de variáveis locais em campos do jogo, como os testes de colisão e o `DroneImpact_tag` das explosões).
- `NF_P32_CHECK`: guardar num `P32` um ponteiro fora da janela (o heap do sistema, outra thread, uma DLL) imprime a
  pilha de chamadas e encerra (código 3), em vez de virar um ponteiro errado.
- Ajudantes: `nf::p32_u(p)` / `nf::p32_ptr<T>(v)` (ponteiro <-> palavra de 32 bits: as palavras "unknown" que são
  ponteiros, as correções dos carregadores, as sentinelas como `0xffffffff`), `nf::p32_load<T>(endereço)` /
  `nf::p32_store(endereço, p)` (uma palavra de ponteiro lida ou escrita por deslocamento), `nf::p32_fn_u`/`p32_fn`
  (funções), `nf::raw(campo)` (o ponteiro nativo de um campo, para `reinterpret_cast`, `auto*` e `?:`; em x86 o próprio
  ponteiro).
- O que fica nativo: a plataforma, o renderizador e a interface, e as estruturas só do port que nunca entram em dados do
  jogo (`ProvisionalObject`, `PcTab`, `MenuHandlerEntry`, `CREDIT_SOURCE`, `CONTROLFUNCS_tag`, a `Simulation` das
  missões de carro).

Regras para código novo (o x64 pega algumas na compilação; as outras são silenciosas lá, então teste as duas versões):
campo de ponteiro numa estrutura do jogo é `P32<T>` (e a estrutura ganha um `static_assert` do tamanho); palavra de
ponteiro por deslocamento é `nf::p32_load`/`nf::p32_store`, nunca `memcpy(&p, base + off, 4)`,
`*reinterpret_cast<T**>` ou `rd<T*>`/`at<T*>` (os leitores por deslocamento recusam ponteiro nativo em x64, pelo
`nf::is_native_ptr_v`: escreva `rd<P32<T>>`); vetores de ponteiros no heap do jogo são `P32<T>*` com `n * 4` bytes;
contas em `float` com `nf::fm::sin` etc., não `std::sin` (abaixo); uma `MATRIX` local montada por `RotMatrix*` ou
`Quat_QuatToMat` só tem a rotação escrita: comece zerada (`MATRIX m{}`) se a matriz inteira for usada depois.

### x64 contra x86

A versão de 64 bits dá os mesmos resultados que a de 32 em todas as conferências (medido em 12/10/2026, os dados do
disco do usuário): `nf_tests` (os mesmos 768 testes, mais 3 da arena: 771, 0 falhas), `nf_level_check` (35 fases, 0
sem resolver), `nf_driving_check` e `nf_driving --check --verify` idênticos (a menos da memória do processo, que inclui
o C runtime estático e a arena), `nf_loader_check` com a saída inteira idêntica (os endereços mascarados), e as 16
capturas de referência (a fase, a introdução com o rosto, a chuva, a neve, o tiroteio, a pausa e o menu principal
Modern, a roda de armas, os resultados, o dossiê, MSAA, 144 Hz, a câmera de segurança, a água) idênticas pixel a pixel,
com os relatórios iguais a menos dos tempos. Também idênticos: um quadro de filme (Media Foundation), o WAV de 14 s do
tiroteio de 07000012 e o codinome gravado no cartão (os três arquivos byte a byte). O desenho do carro do `nf_driving`
varia de uma execução para outra nas duas versões.

Para chegar aí, as duas versões foram comparadas tick a tick (posição, rotação, matriz, tipo e estado de cada objeto, e
as sementes do `Rand`, em todos os ticks do `nf_loader_check`), o que achou:

- **As funções matemáticas de `float` do C runtime.** No de 32 bits, `sinf`, `cosf`, `tanf`, `asinf`, `acosf`, `atanf`,
  `atan2f`, `expf`, `logf`, `log10f` e `powf` são inline sobre a versão `double`; no de 64 bits são funções próprias que
  diferem em 1 ulp numa parte das entradas (medido em 2 milhões de entradas: `asinf` 6%, `acosf` 2%,
  `sinf`/`cosf`/`log10f` 0,1%). As versões `double` dão o mesmo resultado nas duas. `nf::fm::<f>(float)`
  (`src/platform/fmath.h`) calcula na `double` e arredonda: em x86 é o que `std::<f>` já fazia, em x64 dá o resultado do
  x86. `log2f`, `exp2f` e `hypotf` ficam (só a barra da sensibilidade do mouse usa as duas primeiras, e as 61 posições
  dão o mesmo; `hypotf` deu o mesmo que a `double` nas duas).
- **Um erro da versão de 64 bits.** Leitores por deslocamento com um tipo ponteiro (`rd<obj_tag*>(dados + 0x7e8)`,
  `at<M_CONTROL*>(caixa, 0xc)`: 24 lugares em `anim.cpp`, `drone.cpp`, `drone_vision.cpp`, `hud.cpp`, `text.cpp`) liam 8
  bytes de um campo de 4 (em `AnimProcessScriptCmds` a arma na mão deixava de ser reconhecida e o som saía de outro
  lugar). Agora são `P32`, e os modelos recusam ponteiro nativo.
- **Duas leituras de lixo da pilha do original**, que o x64 mostrou porque a pilha dele tem outro conteúdo. O giro do
  cano das torretas (`spin_barrel`) monta `RotMatrixZ` numa matriz da pilha sem a linha da translação, que `Concat` soma
  à posição do cano (com o lixo do x64 o cano e o clarão pulavam 10 ou 100 m). E `DroneWeap_DropWeapon`, num
  personagem sem pose, solta a arma na translação da matriz da mão, que `Mat_Identity` não escreve (no x64 uma arma
  nascia num ponto qualquer de 07000005 e caía para sempre). As duas matrizes agora começam zeradas ou na posição do
  personagem (divergências documentadas nos arquivos); em x86 o lixo dava o mesmo que esses valores, e as conferências
  e capturas continuam idênticas ao `main`.
- **A precisão do x87 na versão de 32 bits** (inerente; fica). Na convenção de chamada x86 uma função devolve `float`
  em `ST(0)`, a pilha do x87, e o MSVC às vezes continua a conta ali, com 53 bits de mantissa, até guardar o resultado:
  `pos.x += Rand_FRand(r) - raio` sai do x87 com um arredondamento só, onde o x64 arredonda cada passo para `float`, e
  funções curtas como os `dot3` devolvem a soma sem arredondar. Um `dumpbin /disasm` dos objetos de 32 bits acha 79
  funções com contas assim (101 lugares; uns 30 na lógica do jogo, o resto nos menus Modern). No rastreio por tick o
  primeiro anel de água de 0700000b já sai 1 ulp diferente no tick 2; essas diferenças não chegaram a nenhuma saída das
  conferências nem a nenhum pixel das capturas, mas a lógica do jogo é caótica e uma conta de 1 ulp pode, numa partida
  longa, virar outra decisão. Prova de que não há mais nada entre as duas: uma versão de 32 bits compilada com `/Gv`
  (`__vectorcall`, que devolve `float` em `XMM0`, sem o x87; um experimento, não está no repositório) dá a mesma saída
  que a de 64 bits em todos os 135.075 ticks do `nf_loader_check`. Fazer a de 64 bits imitar o x87 exigiria reescrever
  cada um desses lugares com `double` conforme o MSVC escolheu o x87 (e cada mudança de código mexe nessa escolha);
  fazer a de 32 bits imitar a de 64 mudaria os resultados dela.
- **Lixo da pilha na coordenada w** (inofensivo). Alguns vetores e matrizes do jogo carregam no `w` o que sobrou na
  pilha (o `pos.w` dos itens que caem, a coluna w da matriz do jogador), como no PS2: o lixo é outro em cada versão, mas
  o desenho força essa coluna (`psiDrawObjectMatrix`, como o PS2) e nenhuma conta em x, y e z o usou nas provas acima.

### Desempenho

`--bench 10` a 1920x1080, duas rodadas alternando as versões (fps médio; entre parênteses a variação das rodadas):

| Cena | x86 | x64 |
|------|-----|-----|
| 0700000c girando, Radeon (iGPU), fora da tela | 237 (226-248) | 236 (228-244) |
| 0700000c girando, Radeon, em janela | 218 (215-220) | 221 (220-222) |
| 07000012 parado, Radeon, fora da tela | 379 (372-387) | 386 (385-387) |
| 0700000c girando, RTX 5090, fora da tela | 2690 (2389-2991) | 4393 (4348-4438) |

Na Radeon, onde a GPU é o limite, as duas versões ficam dentro do ruído. Na RTX 5090, onde o limite é a CPU, a de 64 bits
é cerca de 1,6 vez mais rápida: a lógica custa 0,37 ms por tick contra 0,47-0,53, o `Game_Draw` 0,09 contra 0,14 e o
envio ao Direct3D 0,09 ms por quadro contra 0,16-0,23 (mais registradores, a convenção de chamada de 64 bits e o
Direct3D de 64 bits).

A memória: a arena da versão de 64 bits compromete o que o jogo usa (07000012 parado: 89 MB, o heap do jogo de 64 MB, a
pilha do jogo de 16 MB e o resto do `new`; a linha `arena` do relatório das capturas, só em x64), e o `nf_loader_check`
fica em 108 MB contra 90. Mas o `nightfire.exe` de 64 bits compromete uns 0,8 GB a mais de memória privada (pico de
978 MB contra 211 na RTX 5090, 1226 contra 351 na Radeon), em blocos de 32 MB fora da arena que aparecem quando a fase
cria as texturas e malhas: são do driver de vídeo de 64 bits (o de 32 bits, apertado no espaço de endereços, guarda
menos). O conjunto de trabalho cresce menos (262 contra 220 MB na RTX, 718 contra 332 na Radeon, onde a memória de
vídeo é a do sistema).

### O que falta para o macOS

A plataforma é só Windows: a janela e a entrada (Win32, Raw Input, XInput), o renderizador (Direct3D 11, Direct2D,
DirectWrite, WIC), o som (XAudio2), os filmes (Media Foundation), o relatório de falhas (dbghelp), a reserva da arena e
a troca de pilha (`VirtualAlloc`, o TEB, MASM). O esquema de ponteiros de 4 bytes não depende do Windows: no macOS a
janela começa na imagem do mesmo jeito (com `mmap` perto dela para a arena) e a pilha do jogo pode ser a de uma thread
criada com `pthread_attr_setstack` dentro da arena. Com o clang, `-ffp-contract=off` evita que contas de `float` virem
FMA (o ARM64 contrai por padrão), e as funções matemáticas da libm do macOS diferem das do Windows: para os mesmos
resultados, o port teria de levar as próprias.

## Texturas em alta definição

O executável usa pacotes de texturas feitos para o PCSX2 (a pasta `replacements` do jogo, com
arquivos `.dds`). Com o pacote em `<dados>\textures\SLUS-20579\replacements` (ou em `--textures`),
cada textura carregada procura a sua substituta; T alterna entre as HD e as originais, e o título da
janela mostra quantas foram trocadas. A pasta `textures/` fica fora do git.

O PCSX2 nomeia cada arquivo pelos hashes (XXH3 de 64 bits) do que está na memória do GS:

- a textura: os índices em linha, um byte por texel, quando ela é uma região de um espaço maior (o
  jogo empacota as texturas em espaços na VRAM e as endereça com `CLAMP` em `REGION_REPEAT`) ou é
  menor que um bloco; senão os blocos de 256 bytes como o GS os guarda (PSMT8 em blocos de 16x16,
  PSMT4 em 32x16, com o embaralhamento de colunas do GS), em ordem de linhas de blocos. A posição no
  espaço só existe em tempo de jogo, então os dois hashes são tentados;
- a paleta: as 16 ou 256 cores de 32 bits em ordem lógica (a de 256 cores sem a troca CSM1).

Só o par exato de textura e paleta é aceito: o jogo reaproveita texels com outras paletas, e o pacote
nem sempre tem todas. Com o pacote do usuário, 17.913 das 20.132 texturas das fases (89%) têm
substituta (`nf_loader_check` mostra `hd=` por fase). Os arquivos sem mipmaps (a maioria) são
copiados para RGBA8 na GPU, que gera os mipmaps. O alfa dos pacotes segue o GS (0x80 = opaco), como o
das texturas originais decodificadas; o shader dobra.

## Tabelas montadas na partida (construtores estáticos)

Algumas tabelas do jogo não estão prontas no `.data` do `ACTION.ELF`: no PS2, o `__main` roda
antes do `main` o `__do_global_ctors`, que chama cada função de `__CTOR_LIST__` (`_GLOBAL_$I$...`,
da última para a primeira); cada uma chama um `__static_initialization_and_destruction_0(1, 0xffff)`
que monta tabelas na RAM. A principal é `weapon_data` (115 armas de 0x10c bytes), que o
construtor de 25.720 bytes em 0x001b3488 preenche copiando modelos do `.rodata` e gravando campos.
Como são dados do jogo, o port não as guarda: na partida (`install_game_tables`, `src/game/boot.cpp`)
roda esse mesmo código do ELF do usuário num interpretador MIPS e copia o resultado.

- `src/data/mips_interp.cpp`: interpretador mínimo do R5900 (EE, little-endian) sobre uma RAM de
  32 MB montada com os segmentos `PT_LOAD` do ELF (com `gp = _gp`). Decodifica da codificação só as
  instruções de que esse código precisa: aritmética e lógica de 32 e 64 bits, `mult` de três
  operandos, loads e stores (inclusive os desalinhados `lwl/lwr/ldl/ldr/swl/swr/sdl/sdr` e `lq/sq`
  de 128 bits), desvios com delay slot e os "likely", `j/jal/jr/jalr` e a FPU (`lwc1/swc1`,
  `mtc1/mfc1`, `add/sub/mul/div/mov/neg.s`, conversões, `c.cond.s` com `bc1t/bc1f`), que arredonda
  para zero e satura como a do EE. Instrução desconhecida, acesso fora da RAM, desvio num delay slot
  ou execução fora das funções liberadas param com erro (endereço e palavra crua); nada é ignorado.
- `src/game/static_init.cpp`: libera para interpretação só `__do_global_ctors`, os `_GLOBAL_$I$*` e
  os `__static_initialization_and_destruction_0`; `memset`, `memcpy` e `PS2Sinf` (que o construtor
  de `Copter` usa) são funções nativas. Qualquer outra chamada para com erro. Depois copia, por nome
  de símbolo e conferindo o tamanho no ELF, `weapon_data` (30.820 bytes) e `ammo_data` (408 bytes,
  `.data` comum) para as globais do motor (`src/engine/weapons.h`). O ponteiro de função de cada
  arma (+0x104) nunca é copiado como endereço do PS2: passa por `Weapons_ResolveCode`, que devolve a
  função portada ou 0; no ELF do jogo ele é sempre nulo depois dos construtores. Palavras das
  tabelas que pareçam ponteiros para dentro do ELF (início de função ou variável, começo de string)
  são avisadas; nos dados do jogo não há nenhuma.
- O que cada construtor escreve (rodado sozinho, comparado com a imagem inicial; `nf_loader_check`
  mostra no começo): `SaveIFFBlocks` (ponteiros de funções de gravação), `weapon_data`,
  `SubtitlesInfo`, as tabelas de sprites do HUD (`MissionStatusSprInfo`, `ObjectiveStatusSprInfo`,
  `InfoStatusSprInfo`, `PickupStatusSprInfo` e os `Msg*Pane`) e `COPT_COS`/`COPT_SIN`. Por enquanto
  só as duas tabelas de armas são copiadas; as outras entram na lista quando o código que as usa for
  portado.
- `nf_loader_check` confere fatos independentes da tabela: cada entrada guarda o próprio índice em
  +0, as armas com modelo têm uma pele `05xxxxxx` em +0xdc e o tipo de animação (+0x84) vai de 0 a
  6 (`weapon_data: 95 armas ... em 115 entradas`); falha se o interpretador parar.

## Do disco à tela

1. `ResetMap_Load` (portado) carrega os pacotes com o carregador original. Os blocos viram as
   estruturas do jogo no heap (`celglist_tag`, `cel_tag`, `map_tag`...).
2. `psiCreateMapTextures` e `psiCreateEntityGfx` (portados, `src/platform/psi_gfx.cpp`) montam os
   registros de textura, a tabela `Tex` e os ponteiros do bloco 0x2d como no PS2. No lugar da VRAM
   e das cadeias de DMA, chamam o backend (`src/render/runtime_gfx.cpp`), que decodifica e cria a
   textura no índice de `Tex` e a malha de cada entidade (rígida ou com skinning).
3. A cada quadro de vídeo, o `main` põe a câmera livre na câmera 0 do jogo
   (`glb_viewer[0]`), roda `Camera_UpdateAll` e o `Game_Draw` portado. A visibilidade por portais
   decide as salas, células e objetos; as funções `psi*` de desenho
   (`src/platform/psi_draw.cpp`) registram cada modelo pedido com a matriz, a câmera e o estado de
   mistura; `psiDrawSkinObjectMatrix` registra também uma cópia da paleta de ossos.
4. `RuntimeGfx::collect_frame` converte o registro da câmera 0 na lista do renderizador e
   acrescenta os objetos cujo `*_Create` ainda não foi portado (`g_provisional_objects`), que o jogo
   não conhece e por isso passam sem visibilidade. Durante uma cena com câmera (scripts de cena e
   NIS), o jogo desliga a câmera 0 e desenha pela 4; `Script_ActiveCamera` avisa o `main`, que então
   usa o registro e a matriz da câmera 4, com o campo de visão vertical dela (o do quadro-chave da
   cena) e a proporção da janela. O relatório do `--screenshot` mostra `camera_script`.

No ponto de partida de cada fase, as imagens são iguais às do commit anterior (que desenhava todas
as células) a não ser onde o jogo faz diferente: o céu fica em volta da câmera e os marcadores de
editor não aparecem.

## Modelos com skinning

Personagens e outros modelos animados por ossos (blocos 0x2d com a estrutura de skinning, ver
`docs/formats.md`, "Modelos com skinning") são desenhados como no PS2 pelo `psiDrawSkinObjectMatrix`,
que o `AnimObjectDraw` do motor chama com a paleta montada por `psiBuildMatrixPalette`.

- Decodificação (`nf::decode_gfx`, `src/data/map_item.cpp`): emula as escritas da VIF1 na memória da
  VU1 como o exportador Python e tira posição, normal, ossos e peso da entrada do `SkinIt`, na pose
  de repouso; UV, cor, textura e tiras como nas malhas rígidas. Só a primeira caixa (o PS2 só
  desenha ela; todas as malhas do disco têm uma). O slot de cada vértice já sai traduzido pela
  lista de ossos da malha para o índice do osso no esqueleto, e o índice de morph do vértice (o rosto; ver
  "Rostos", abaixo). Nos dados do jogo: 582 malhas com skinning nas 36 fases, todas decodificadas sem erro
  (os mesmos 1.483.501 vértices e 1.077.257 triângulos do exportador); `nf_loader_check` mostra
  `skin=ok/total` por fase.
- Vértice (`SkinnedVertex`, 48 bytes): posição, normal, UV, cor RGBA8, dois ossos do esqueleto (u8),
  o peso do primeiro (o segundo pesa 1 - peso; no disco sempre em [0, 1]) e o índice de morph (-1: nenhum).
- Paleta: uma matriz 4x4 vetor-linha por osso do esqueleto, já no mundo e com a pose inversa (um
  vértice `p` da pose de repouso vai a `p * paleta[osso]`). O constant buffer tem 128 matrizes
  (`kMaxSkinBones`); as listas de ossos têm até 55 entradas (a VU0 comporta 64) e o maior índice é
  72 (esqueletos de até 73 ossos). Cada desenho copia `paleta[0 .. maior osso da lista]`.
- `psiDrawSkinObjectMatrix` (`src/platform/psi_draw.cpp`) segue o original: recorta pela esfera do
  objeto (no PC, o cubo em volta dela contra os quatro lados do frustum da câmera, no lugar das
  matrizes de recorte da VU0), exige `obj->sphere_src` (os dados de animação), e reaproveita no mesmo
  quadro o que já transformou para o mesmo objeto (o `SkinCache` procura só pelo objeto). A matriz
  recebida é ignorada, como no PS2.
- No renderizador, malha e desenho têm de combinar: um modelo com skinning pedido por
  `psiDrawObjectMatrix` (ou um provisório) não é desenhado, como antes.

### Rostos

As cabeças dos personagens mexem a boca com a fala, piscam e mudam de expressão pelos 22 alvos de morph do bloco 0x2B
(formato em docs/formats.md, "Morphs do rosto"; a lógica dos pesos em docs/engine.md, "Rostos": `AnimFacialUpdate`,
`AnimFacialBlend`, `AnimObjectHeadTrack`, a escolha dos 8 maiores em `AnimObjectDraw`). No PS2 a soma é feita no EE,
dentro do `SkinIt`, antes do skinning da VU0. No PC ela vai para a GPU:

- **Na carga**, `RuntimeGfx::on_entity` lê o bloco 0x2B da entidade (`entgfx_tag::morph`, o tamanho no cabeçalho do
  bloco) com `nf::decode_morph` e confere que os deltas de cada vértice cabem nele; o `Renderer` guarda os deltas
  de todos os elementos num `StructuredBuffer<float3>` por malha (60 a 75 KB por cabeça; 63 cabeças no disco, até
  39 numa fase). Os elementos do cabeçalho vão zerados (a VU0 trata os desnormalizados como 0).
- **A cada desenho**, `psiDrawSkinObjectMatrix` copia os pesos em vigor (`Morph_Weight[k]` no alvo
  `Morph_Index[k]`, 0 nos outros: 22 floats) para `g_psi_frame.morphs`, com as mesmas condições do PS2 (a malha com
  o bloco, `Morph_Count != 0` e, fora de cena, o menor byte de closeness até 2); `collect_frame` os põe em
  `DrawItem::morph`.
- **O shader**: um desenho com algum peso diferente de 0 usa `vs_skin_morph`, uma variante de `vs_skin` que lê o
  índice de morph do vértice (`MORPHINDEX`) e, se for >= 0, soma `peso[k] x delta[m + k]` à posição de repouso
  antes do skinning (a mesma conta de `nf::morph_vertex`, que os testes usam); a normal não muda, como no PS2. Os
  desenhos sem pesos (e os modelos sem rosto) continuam no `vs_skin` de antes: as imagens sem rosto são idênticas.
  Custo: só os ~600 vértices com morph de uma cabeça (de ~2.000) fazem até 22 leituras do buffer, mais um
  constant buffer de 368 bytes por desenho; numa RTX 5090, 900 quadros da cena de 07000001 a 2560x1440 levaram o
  mesmo tempo com e sem os morphs (1,9 s). Nada na CPU além do que o jogo já fazia (os pesos por tick).
- **Entre ticks** (`render/frame_interp.h`), os pesos de um rosto que tem par no tick anterior são interpolados em
  linha reta, peso a peso (`interp_weights`; um lado sem pesos conta como zeros), como a paleta; nas pontas, os do
  tick. O relatório `ritmo` não mudou; `InterpStats::morphs` conta os rostos interpolados.
- O caminho Direct3D 9 do RTX Remix (`--renderer d3d9ff`) não aplica os morphs (desenha a pose de repouso).

Depuração: `--no-morphs` desenha a pose de repouso (como antes do port dos rostos; a lógica dos pesos continua). O
relatório da captura tem uma linha `rosto desenhados N com_pesos M | dist ... pos ... giro ... tipo ... pele ...
estado ... timer ... olhos x y cabeca flags giro inclinação scripts e0/e1/e2 pesos alvo:peso ...` (até 4 rostos, do
mais perto); o `--frames-dump` acrescenta ao `frames.tsv` as colunas `rosto_dist`, `rosto_pele`, `rosto_estado` e
`rosto_pesos` (os 22 pesos desenhados, interpolados) do rosto com pesos mais perto, ou, com `NF_FACE_SKIN=<pele em
hexa>`, do rosto daquela pele. `python tools/morph.py mouth <pacote> <item da cabeça> frames.tsv --wav audio.wav
--skin <pele>` mede a abertura da boca (o lábio de baixo, pelos deltas do alvo 21) quadro a quadro e a correlação com
a energia da voz do `--audio-dump` (o mesmo relógio).

Na cena de abertura de 07000001 (Mayhew, pele 0500004f, cabeça 01000145), com a música em 0 (`[game] music_volume
= 0` num `--config`) e `--fixed-video-hz 60 --wait 9.5 --frames 480`: a boca abre no quadro em que a voz começa
("Ah, Mr. Bond", 10,3 s: de 0 a 6,6 mm em 0,1 s, com o RMS da voz de -44 a -20 dB), fecha na pausa de 15,0 a 15,5 s e segue
a fala; a correlação entre a abertura e o RMS da voz é r = 0,47 (0,49 com a boca 2 quadros depois da voz).

## Estado do GS, tweak e partículas

O PS2 decide a mistura, o teste de alfa e o Z de cada lote pelos registradores do GS. O
renderizador reproduz isso a partir de três fontes, nesta ordem de prioridade:

1. O que a cadeia da própria malha escreve (pares A+D de `ALPHA_1`, `TEST_1` e `ZBUF_1` nos pacotes
   `DIRECT`, ver `docs/formats.md`): `nf::decode_gfx` guarda em cada parte (`SubMesh::gs`) o estado
   em vigor no `MSCNT` do lote e o bit ABE do PRIM da GIF tag. As partes seguem a ordem da cadeia
   (lotes seguidos com a mesma textura e o mesmo estado viram uma parte), que é a ordem em que o GS
   desenha: importa nas partes misturadas e nas máscaras de alfa (`0x64` grava só o alfa, `0x58`
   soma pela máscara). Nos dados, todo lote com vértices escreve os três registradores (0 lotes
   dependem do estado deixado por outro desenho; `nf_loader_check` conta e mostra a tabela).
2. O estado do desenho registrado por `psi_draw` (`DrawItem::gs`): o `ALPHA_1` de
   `psiSetUpColourBlend` (0 → 0x48, 1 → 0x44, 2 → 0x2a, 3 → 0x42) e, se já foram chamados no quadro,
   `TEST_1`/`ZBUF_1` de `psiSwitchZBufferRead/Write`.
3. Para o que ninguém escreveu, a regra antiga: opaco descarta alfa < 0.5 e escreve Z; misturado
   descarta alfa < 0.004 e não escreve Z.

A equação `(A - B) * C + D` vira fatores de mistura (`nf::gs_blend_equation`, testada em
`tests/test_gs_state.cpp`): `0x44` é a transparência comum, `0x48` aditiva, `0x42` subtrativa,
`0x2a` ou ABE = 0 sem mistura, `FIX` vai no blend factor, `Ad` lê o alfa do alvo. Como no GS, o alfa
gravado no alvo é o do pixel (As) em todos os desenhos, e a mistura só mexe em r, g e b. O teste de
alfa (`ATST`/`AREF`, no shader, com o alfa inteiro na escala do GS, `0x80` = 1.0) acontece antes da
mistura; `ZTE`/`ZTST` e `ZMSK` escolhem o depth state. O alfa de cada pixel é o da textura vezes o da
cor do vértice vezes o do tweak, como `TEX0.TFX = MODULATE` com `TCC = 1` faz no GS.

Comparando com o commit anterior (que usava só `psiSetUpColourBlend`, com teste de alfa em 50% nos
opacos e sem o alfa do vértice): o que tem alfa de vértice abaixo de 0x80 passa a ficar translúcido
como no PS2 (água, a segunda textura que se mistura no chão, as sombras suaves sob móveis, o papel
dos shoji de 07000001, que fica bem transparente), os efeitos aditivos que antes eram quadrados
opacos (o clarão do tiro, brilhos de luminárias, as "chamas" vermelhas e amarelas que cobriam
paredes e camas em 07000015 e 07000002) passam a somar luz, e as marcas de bala escurecem.

Tweak (`psiSetTweakARGB`): o registro de cada desenho tem a, r, g, b (`DrawItem::tint`, valor / 255,
o `TAmbient` do PS2). Como a VU1, o vertex shader multiplica por ele a cor e o alfa de cada vértice; com
luzes ele é o termo ambiente (ver "Iluminação e névoa" abaixo).

Partículas (`psiDrawParticleList`): o registro tem as posições, as cores (escala do GS) e o tamanho
no mundo (`PsiParticleList::half_size`, o tamanho que o sprite tem no PS2 a 60 graus de campo de
visão). `RuntimeGfx::collect_frame` monta um quadrado virado para a câmera por partícula (direita e
cima da tela, a textura girada 180 graus como no PS2) e descarta as partículas cujo centro fica a
menos de 0.1 m da câmera ou fora do dobro da tela, como o microprograma. O renderizador desenha os
quadrados com um vertex buffer dinâmico, na posição do desenho na ordem do `Game_Draw`, com o estado
de `psiDrawParticleList` (alfa > 0, Z testado sem escrita, aditiva ou transparente); o shader modula
a textura como o GS (`Ct * Cv / 128`, `At * Av / 128`, alfa até 2.0) e, na aditiva, multiplica a cor
pelo alfa (fator ONE), para que alfa acima de 1 continue somando mais. O tweak não entra (o
microprograma de partículas não o usa).

Estilhaços de vidro (`psiDrawShard`, `src/platform/psi_shard.cpp`): no PS2 cada estilhaço é um
triângulo com cadeia de DMA própria (`psiCreateShard`: posição, cor inteira e UV dos três vértices),
chamada com a matriz dele e a textura do triângulo. No PC a cadeia é montada igual no heap, e
`psiDrawShard` lê dela os três vértices, leva-os ao mundo pela matriz e registra um triângulo solto
(`psi_draw_triangles`: `PsiFrame::tri_vertices`/`tri_lists`, `PsiDrawCall::triangles`) com a câmera, a
mistura e os registradores do GS em vigor. `RuntimeGfx::collect_frame` junta chamadas seguidas com a
mesma textura e o mesmo estado num `DrawItem::triangles`, e `Renderer::draw_triangles` desenha os
vértices de um vertex buffer dinâmico, sem índices, com o shader dos sprites: a cor na escala do GS
sem tweak, como a rotina 0 (`ROTATE_FAST`) que o pacote da VU1 dos estilhaços escolhe (o alfa que
`DrawAllShards` põe no tweak não muda nada, nem no PS2). O estado vem de `psiSetShardRenderStates`:
teste de Z sem escrita, alfa > 0 e mistura transparente (0x44); a textura é a da entrada de `Tex` cujo
pacote é o do triângulo (`GetTextureFromDMA`, guardada por `psiFont_SetTexture`).

### A arma em primeira pessoa e a limpeza do Z

A arma na mão, os braços e o clarão do tiro são a lista 2 do `Game_Draw` (objetos com a flag 0x2000; a arma tem
0x1a0a0). No PS2, depois dos transparentes das câmeras 0-3, o `Game_Draw` põe a câmera 5 em uso e chama `psiClearZ`,
que manda o `ZClearPacket` (`CreateZClearSprite`, montado em `psiPreGameRun`): `TEST_1` = 0x32001 (alfa NEVER com
AFAIL = só Z, `ZTST` ALWAYS), `ZBUF_1` com escrita e um SPRITE da tela inteira com Z = 1, o mais longe para o teste
GEQUAL do GS. A cor fica intacta e o Z fica limpo, então a lista 2 nunca é cortada pelo cenário e só se testa contra
ela mesma (o braço na frente da arma, o clarão atrás dela). Só acontece com algo nas listas 2 (`Tots[2]`) e com
`WipeZBuffer` ligado (o valor inicial; nada no ELF o desliga); as câmeras 4-9 desenham a lista 2 delas sem limpar.

O port antes só contava a chamada (`PsiFrame::clear_z`) e o renderizador desenhava a lista 2 contra o Z das salas: perto
de uma parede ou de um objeto, a arma e às vezes o braço sumiam dentro dele. Agora `psiClearZ` também grava onde caiu
(`PsiFrame::clear_z_at`: antes de `draws[i]`), o `collect_frame` põe ali um item `DrawItem::clear_depth`
(`render/frame_markers.h`, junto com o da névoa: no mesmo índice a névoa vem antes, porque ela lê o Z) e o renderizador
limpa a profundidade da cena nesse ponto (no alvo da cena, com MSAA ou escala; no caminho Direct3D 9 também). A limpeza
do começo do quadro (`psiPreDraw`) não vira item. Na câmera livre (F2, `--cam`; só do port) o `main.cpp` tira os itens
de limpeza: a arma fica na cabeça do jogador (`Player_PositionGun`), não diante dessa câmera, e aparece onde está no
mundo. Os objetos provisórios entram antes da limpeza, e a interpolação entre
ticks põe os desenhos mantidos do tick anterior nas listas 0 e 1, também antes dela. Longe de paredes a imagem é a mesma
de antes (0 pixels de diferença em 07000001, 07000012 e 0700000c no começo da fase). A linha `luzes` do relatório das
capturas termina com `limpa_z <índices> lista2 N` (os desenhos da lista 2 no quadro).

```bat
build\Release\nightfire.exe --level 07000012 --at 20.5,1.09,19.155,3.1416 --input "none:0.5 W:2" --wait 3 --screenshot out\shots\parede.png
```

## Camada 2D

Os sprites, o texto (legendas, mensagens) e o HUD passam pelo código do jogo
(`Sprite_*`, `Text_*`, `Font_*`, `View_DrawSprites`; ver `docs/engine.md`, "Camada 2D") até
`psiDrawSprites`, que no PS2 manda cada `SPRITE_DRAW` ao GS e no PC (`src/platform/psi_2d.cpp`) o registra:

- Registro (`PsiFrame::quads2d`/`lists2d`, um `PsiDrawCall` com `sprites2d` por chamada, na ordem do
  `Game_Draw`): para cada elemento, o retângulo na tela do PS2 (pixels de 512 x 448, como o XYZ2 do GS
  sem o deslocamento da tela, com a mesma truncagem em 1/16 de pixel), o ST, a cor na escala do GS (alfa
  já dividido por 2), a textura (índice de `Tex`), o ALPHA_1 (0x44, 0x48 ou 0x42) e o filtro (de ponto nas
  três texturas de fonte, `FontList`; bilinear no resto). O estado em vigor vem de `psiStartSprites`:
  alfa > 0, sem teste nem escrita de Z.
- Origem (`Psi2DOrigin`): o que o elemento é (imagem, glifo, símbolo de botão), a passada (principal,
  sombra, contorno), o `sprite` do jogo, o hashcode da textura (`03xxxxxx`), a fonte, o caractere, o
  texto e a posição nele. `View_DrawSprites` e `__Font_DrawText` a preenchem numa lista paralela a
  `SprBuffList` (`SprOriginList`) e `psiDrawSprites` a copia.
- Tela: `RuntimeGfx::collect_frame` leva cada retângulo para a janela por `psi_2d_viewport`. Por padrão,
  a tela do PS2 (que a TV mostra em 4:3) é uma área 4:3 centrada, escalada pela altura (numa janela mais
  estreita que 4:3, pela largura): x = esquerda + x_PS2 * largura / 512, y = y_PS2 * altura / 448. O 2D é
  recortado nessa área, como pelo SCISSOR do GS (a cena 3D, em Hor+, continua dos lados). Só no PC
  (`psi_2d_map_rect`): um elemento da largura inteira da tela do PS2 (x de 0 ou menos a 512 ou mais: as
  tarjas de cinema, a cortina de sangue, as setas de dano, as telas de luneta e de visão noturna) vai de
  borda a borda da janela, sem esse recorte; numa janela mais estreita que 4:3, o mesmo para a altura
  inteira. Os painéis do HUD ficam onde a tela do PS2 os põe, dentro da área 4:3.
- O 2D de todas as câmeras entra (no PS2 a tela junta as câmeras: o HUD e as legendas ficam na 5, as
  tarjas de cinema na 4), por cima do que veio antes, depois da névoa.
- Renderizador (`Renderer::draw_2d`): quatro vértices por elemento, já na tela; elementos seguidos com a
  mesma textura, ALPHA_1 e filtro saem num desenho só; o pixel shader é o dos sprites (MODULATE do GS,
  teste de alfa > 0); só o nível 0 da textura. Uma substituta HD do pacote do PCSX2 é amostrada com
  filtro bilinear.

A camada 2D em alta definição (abaixo) usa esse registro: com ela ligada, `collect_frame` passa o 2D do
quadro por `ui_hd_build` em vez de `add_2d`.

## Camada 2D em alta definição

Opcional (`--hd-ui`, tecla F4): refaz o HUD, as mensagens, as legendas e o que mais passar por
`psiDrawSprites` (os menus, quando existirem) na resolução real da janela, sem mexer na lógica. A posição,
o tamanho, a cor, o alfa, o tempo, a quebra de linha e as páginas continuam sendo os que o jogo decidiu;
o desenho original continua o padrão e a referência.

Peças (`src/render`):

- `ui_hd.h/.cpp` (sem GPU; biblioteca `nf_ui_hd`, testada em `tests/test_ui_hd.cpp`): lê o `map.json`,
  junta os glifos em textos, escolhe as substitutas, calcula as âncoras e monta um `UiHdBatch` por chamada
  de `psiDrawSprites` (`ui_hd_build`, sobre o registro do quadro inteiro).
- `ui_hd_game.cpp`: o que a camada precisa do jogo a cada quadro (`UiHdEnv`): o tamanho das texturas
  `03xxxxxx` pela tabela de hashcodes, a identidade dos sprites do HUD (pane e índice, a mira é a pane -1,
  com o retângulo de cada pane) e a altura do 'H' de cada fonte do jogo.
- `ui_hd_draw.h/.cpp`: Direct2D 1.1, DirectWrite e WIC no dispositivo Direct3D 11 do renderizador (criado
  com `D3D11_CREATE_DEVICE_BGRA_SUPPORT`).
- `ui_hd_app.h/.cpp`: acha a pasta `hd_ui`, lê o `map.json`, liga e desliga (F4) e faz o inventário.
- O renderizador desenha cada lote na posição dele na lista (`DrawItem::ui`): as imagens pelo mesmo
  caminho do 2D original (`draw_2d`, com a substituta no lugar da textura), os textos com Direct2D no mesmo
  alvo, depois do qual o estado do Direct3D do quadro volta (`bind_frame_state`).

Textos: os glifos seguidos de uma mesma chamada de `__Font_DrawText` (`Psi2DOrigin::run`, um contador por
chamada; a origem também guarda o alinhamento, a escala e a linha de base da caneta) voltam a ser o texto:
o caractere de cada glifo sai do próprio texto (`char_index`, contando como `__Font_DrawText`: o `~X` gasta
dois bytes), os espaços entre eles também, e cada linha é uma linha de base (os `\n` do texto já
quebrados pelo jogo). A cor é a de cada glifo (o `0xba` troca no meio da linha), na escala do GS (a textura
das fontes é branca: `0x80` = 1,0). 0x92, 0x85 e 0x99 voltam a ser ’, … e ™ (a fonte do jogo os trocava
por falta de glifo). As cópias de sombra e contorno que `Font_DrawText` desenha antes do texto viram
efeitos dele: quatro cópias em (±1, ±1) na mesma cor viram um traço de 1 pixel do PS2 para fora em volta
dos glifos; a sombra (+1, +1) e o realce (-1, -1) viram cópias deslocadas. O DirectWrite desenha cada
linha na fonte que o `map.json` dá à fonte do jogo, no tamanho em que a altura das maiúsculas da fonte
vetorial (`capHeight` da fonte) é a do 'H' da fonte do jogo vezes a escala do texto e a da tela; o bloco
fica no lugar dos glifos originais com o alinhamento do formato (as linhas de um bloco começam todas na
mesma caneta, como no jogo; centrado ou à direita, o bloco inteiro se move) e, se ficar mais largo que o
original, é comprimido na horizontal até `fit_min`. Um símbolo de botão (`~X`) continua uma imagem (o
desenho do aparelho em uso, ou uma tecla: "Símbolos de botão por aparelho"), e os trechos de texto em volta
dele ficam presos à esquerda de onde estavam (uma tecla mais larga os empurra). Glifos com mistura aditiva
usam a mistura aditiva do Direct2D.

Imagens: cada quadrado continua um quadrado do 2D original, com a cor, o alfa, a mistura (`0x100`
aditiva, `0x200` subtrativa), o espelhamento e os ladrilhos dele; só a textura muda quando o `map.json` tem
uma substituta para ele. Sem substituta, a textura original (ou a do pacote do PCSX2), com filtro bilinear
e mipmaps no lugar do filtro de ponto das fontes. A substituta é rasterizada no tamanho em que aparece
(arredondado para cima em degraus de até 12,5%, para não refazer a cada pixel de uma animação), com a
cor sem o alfa pré-multiplicado e o alfa na escala do GS, como as texturas do jogo.

Âncoras (`"anchor": "edges"` ou `--hd-ui-anchor edges`): numa janela mais larga que 4:3, os elementos do
HUD andam para as bordas. Os quadrados de uma mesma pane que se encostam (até 8 pixels da tela do PS2)
formam um grupo, e o terço do grupo decide: no terço esquerdo da tela do PS2 vai para a borda esquerda da
janela, no direito para a direita, no meio fica (numa janela mais estreita, o mesmo em y). Assim o
medidor de vida inteiro vai para a esquerda, a munição para a direita, a faixa das mensagens com as pontas
fica no meio, e o ícone de toque (pane 1, longe do medidor) vai para a direita sozinho. O recorte desses
elementos passa a ser a janela. Elementos da largura da tela continuam esticados; o que não é de pane
(legendas, tarjas) fica na área 4:3, como as panes e os sprites de `anchor_skip` (padrão: a pane 8, o
reflexo do sol, e o sprite 7 da pane 0, o alvo das armas 0x50/0x51, que acompanham pontos da cena).
(`HUDPANE_tag` tem largura e altura, mas elas ficam 0 nas panes do disco, e os sprites de várias panes
são posicionados pela atualização, não pela descrição; por isso os grupos são feitos a cada quadro.)

### `hd_ui/map.json`

```json
{
  "anchor": "centre",
  "anchor_skip": [8, [0, 7]],
  "text": true,
  "fonts": {
    "2": {"name": "SerpLight", "family": "Bahnschrift", "weight": 600, "stretch": 5, "italic": false,
          "size": 1.0, "spacing": 0.04, "fit_min": 0.7}
  },
  "images": [
    {"name": "mira", "texture": "03000026", "rect": [0, 0, 31, 32], "file": "svg/crosshair.svg"},
    {"name": "balas", "texture": "03000128", "rect": [26, 0, 15, 256], "extent": ["inclusive", "exclusive"],
     "file": "svg/bullets_column.svg"},
    {"name": "vida: lâmina 1", "pane": 1, "sprite": 8, "texture": "0300007a", "file": "svg/health_blade_8.svg"}
  ]
}
```

- `anchor`: `centre` ou `edges`; `anchor_skip`: panes (números) e sprites (`[pane, sprite]`) fora das
  âncoras.
- `fonts`: uma entrada por fonte do jogo (`1` NFont2, `2` SerpLight, `3` Medium): família DirectWrite, peso
  (100 a 950), largura (`stretch`, 1 a 9), itálico, `size` (multiplica o tamanho calculado), `spacing`
  (espaço a mais entre os caracteres, em em) e `fit_min` (menor compressão horizontal). As fontes da pasta
  `hd_ui/fonts/` (`.ttf`, `.otf`, `.ttc`; fora do git, porque podem ter licença própria) entram antes das do
  sistema. Família não encontrada: Bahnschrift, depois Segoe UI.
- `images`: a chave é o hashcode da textura (`texture`) com um retângulo em texels (`rect`: u, v, largura,
  altura), e/ou a identidade no HUD (`pane` 0 a 21 ou -1 para a mira, `sprite` o índice na pane). Uma
  entrada com `pane`/`sprite` vale antes das só de textura; entre iguais, a primeira do arquivo. O elemento
  casa se a parte da textura que ele mostra fica dentro do retângulo; a substituta inteira cobre o
  retângulo, e o elemento mostra o pedaço dele (a pilha de balas mostra só as balas do pente; o quadro da
  mira em uso, só o pedaço dele). Sem retângulo, com identidade, a substituta cobre o elemento inteiro.
  `extent` diz como o recorte cobre os texels, para os dois eixos ou `[u, v]`: `inclusive` (padrão: `u` e
  `u + uw` são os centros do primeiro e do último texel, como os sprites com `uw = largura - 1`; a
  substituta vai do centro do primeiro texel da chave ao do último) ou `exclusive` (`uw` texels a partir
  de `u`, como as balas da pilha).
- `prompts` (opcional): `{"ps2": {"A": "svg/ps2_cross.svg", ...}, "xbox": {"A": "svg/xbox_a.svg", "LB": ...}}`,
  o desenho de cada símbolo de botão no conjunto do PS2 (pelo código depois do `~`) e de cada botão do Xbox
  (A, B, X, Y, LB, RB, LT, RT, LS, RS, Menu, View, DpadV, DpadH, Dpad); ver "Símbolos de botão por aparelho".
  Essas entradas não entram na busca por textura.
- `file`: `.svg` (Direct2D, Windows 10 1703 ou mais novo; `width`/`height` em 100% e
  `preserveAspectRatio="none"`, para o desenho ocupar o tamanho em que é rasterizado) ou `.png` com alfa
  (WIC), relativo à pasta `hd_ui`. As cores do desenho são as finais: o elemento ainda multiplica pela cor
  do sprite (o verde, o amarelo e o vermelho das lâminas da vida vêm do sprite; por isso elas são cinza).
  Uma substituta que não carrega volta para a textura original.

Para fazer uma substituta nova: rodar com `--ui-inventory out/ui_crops` e passar pelo elemento; o
`inventory.tsv` diz o hashcode, o recorte (u, v, extensão), o tamanho da textura e da tela, quantos quadros
e de que pane:sprite ele é, e `out/ui_crops` tem o recorte e a textura inteira em PNG (fora do git: são
dados do jogo). Desenhar o SVG com o `viewBox` nas coordenadas do retângulo da chave (ou da tela, numa
chave por identidade), acrescentar a entrada no `map.json` e conferir com `--hd-ui --screenshot`. Com a
variável de ambiente `NF_UI_HD_DUMP=<pasta>`, cada rasterização de SVG vira um PNG nessa pasta.
`python tools/ui_hd_assets.py` gera as substitutas atuais (geometria própria, sem copiar as texturas) e
`--check` mede as lâminas da vida contra os retângulos dos sprites.

Fontes escolhidas (todas vêm com o Windows 10 e 11): as três fontes do jogo têm o mesmo desenho quadrado
de cantos arredondados, que a Bahnschrift (uma DIN) segue de perto. NFont2 e SerpLight: Bahnschrift
SemiBold (a do jogo, ampliada em pixels, parece mais pesada que a Regular); Medium: Bahnschrift Bold
itálico (o DirectWrite inclina a Bold, que não tem itálico próprio). Espaço entre caracteres de 0,04 em,
porque a fonte do jogo é mais aberta.

Substitutas feitas (`hd_ui/svg`, geradas por `tools/ui_hd_assets.py`): a mira (o círculo e a cruz
tracejada de `03000026`; o quadro em uso sai do mesmo desenho), a faixa das mensagens (as duas pontas e o
meio esticado de `0300007a`), a pilha de balas da munição de 9 mm (a coluna `[26, 0, 15, 256]` de
`03000128`) e o medidor de vida (as sete lâminas em espiral, sprites 8 a 14 da pane 1, e o anel, sprite
15: uma íris de lâminas entre espirais `theta = a - b / r`, que se encaixam como as do jogo). Dos menus
(ver "Inventário dos menus" abaixo): o painel (moldura cor de bronze com o fio vermelho, a faixa dourada das
abas com brilho, o vidro vermelho escuro translúcido e a barra de ajuda laranja, escura à direita), a
silhueta da pausa (luzes de festa desfocadas e um homem erguendo uma taça de martini), o botão cinza de
borda laranja (o cursor das barras), o rádio, as quatro setas laranja, as caixas de marcar (ligada laranja
com o visto, desligada cinza), o destaque de linha, a barra de volume em segmentos, as setas grandes, o 007
com a arma e o canto escuro do resultado, e os símbolos de botão do PS2 e do Xbox (as letras são traços
próprios: o Direct2D não desenha `<text>` de SVG). Nada é traçado das texturas: as medidas vêm do
inventário e as formas são desenhadas no gerador.

Ainda na textura original. Vistos pelo inventário (07000001 com objetivo, item e legenda; 07000012 ferido,
morto, agachado e mirando; começo de 07000016 e 0700000a):

| Elemento | Textura e recorte | Pane:sprite |
|---|---|---|
| cortina de sangue: borda (mistura subtrativa) | `0300005a` [0, 0, 127, 31] | 12:0 |
| cortina de sangue e tarjas de cinema: o preto | `03000002` (32 x 32) | 12:1 e câmera 4 |
| clarão vermelho da dor (vinheta aditiva de tela cheia) | `0300018e` (64 x 64) | 1:22, 1:23 |
| ícone de agachar | `03000173` (64 x 64; `03000174` ao levantar) | 1:19 |
| balas de outras munições | `03000128` [39, 222, 25, 12] e [45, 222, 19, 20] | 0:5, 0:6 |

Não vistos nessas capturas, mas no código do HUD: as oito lâminas cor de cobre do colete (pane 1, sprites 0
a 7, no mesmo `0300007a`), as outras colunas de `03000128`, os ícones de toque (`03000064` a `0300006a`),
o 007 girando do momento Bond (1:16), as telas de luneta, visão noturna, raio X, câmera e câmera de segurança, o
reflexo do sol, o ar, o radar e o placar do multijogador. O preto puro (`03000002`) não precisa de
substituta. Dos menus, só o logotipo "007 nightfire" do resultado (`0300016d`) continua na textura.

Limitações: o texto vetorial não tem a largura exata da fonte do jogo (o bloco fica no lugar e é
comprimido se passar da largura original, mas uma linha mais curta não é esticada); as âncoras são por
grupo de sprites que se encostam numa pane; o Direct2D é necessário (sem ele a camada não liga e o 2D original continua);
desenhar um texto por quadro cria um layout do DirectWrite por linha (rápido, mas sem cache).

### Inventário dos menus

Visto com `--ui-inventory` na pausa (as quatro abas e a caixa SIM/NÃO do RESTART), no "Mission Failed"
(07000012 morre aos ~9 s: `--input "none:10 space:0.3"`), no resultado (`--win 1 --shot-level 07000048`) e
nas páginas do PC (central, vídeo, controles e teclas: `--open-menu-page 40000060..63`). Recortes em texels
(u, v, extensão, como o `inventory.tsv`: `uw = 16` são 17 centros de texel) e o tamanho na tela do PS2:

| Peça | Textura e recorte | Na tela | Como é esticada | Onde | Substituta |
|---|---|---|---|---|---|
| painel: moldura, faixa das abas (título), vidro e barra de ajuda, tudo numa imagem | `03000070` [0, 0, 511, 255] (512 x 256) | 440 x 248 (também nas opções do PC) | rótulo de imagem (`M_LABEL`, mensagem 0x24 e o recorte 0x2d): a textura inteira no retângulo do controle; agora em nove pedaços fora da proporção | pausa, fim de missão, caixa SIM/NÃO, páginas do PC | `menu_panel.svg` |
| silhueta com luzes de festa | `030000db` [0, 0, 127, 127] (128 x 128) | 161 x 203 | rótulo de imagem, aditivo (0x100: ALPHA 0x48), alfa 0x50 | pausa, fim de missão | `menu_silhouette.svg` |
| seta da linha escolhida | `03000042` [48, 16, 16, 16] | 16 x 16 | peça fixa do destaque da lista (conjunto 4) | listas da pausa, do fim e do PC | `menu_arrow_right.svg` |
| setas cima/baixo | `03000042` [16, 0, 16, 16], [32, 0, 16, 16] | 16 x 16 | peças fixas | aba CONTROLS (estilo de botões) | `menu_arrow_up/down.svg` |
| setas esquerda/direita da barra | `03000042` [48, 0, 16, 16], [48, 16, 16, 16] | 16 x 16 | peças fixas (conjunto 5) | página de controles | `menu_arrow_left/right.svg` |
| cursor da barra | `03000042` [0, 0, 16, 2] + [8, 7, 2, 2] + [0, 14, 16, 2] | 16 x 2, 16 x 12, 16 x 2 | três pedaços do componente: as linhas de cima e de baixo fixas, o meio (2 x 2 texels) esticado | página de controles | `menu_button.svg` (o quadrado inteiro [0, 0, 16, 16]) |
| caixa de marcar ligada / desligada | `03000042` [25, 19, 2, 2] / [31, 19, 2, 2] | 12 x 12 | uma amostra de cor (2 x 2 texels) esticada (conjunto 3) | página de controles | `menu_check_on/off.svg` |
| destaque da linha | `03000042` [38, 19, 1, 1] (branco) | 372 x 25 | um texel esticado, com a cor do sprite | aba CONTROLS (a linha do estilo) | `menu_highlight.svg` |
| símbolos de botão dos textos (`~A`...) | `03000075` (256 x 128; tabela `specialchar` do ELF, abaixo) | 7 a 20 de lado | imagem do tamanho do símbolo, no meio do texto | barras de ajuda, aba CONTROLS, objetivos, resultado | `ps2_*.svg`, `xbox_*.svg`, `mark_*.svg`, teclas |
| 007 com a arma | `03000140` [0, 0, 255, 127] (256 x 128) | 296 x 173 | rótulo de imagem | resultado | `results_007.svg` |
| canto escuro com o arco cor de cobre | `0300014c` [1, 1, 255, 255] (256 x 256) | 255 x 255 | rótulo de imagem | resultado | `results_corner.svg` |
| logotipo "007 nightfire" | `0300016d` [0, 0, 255, 127] (256 x 128) | 124 x 40 | rótulo de imagem | resultado | ainda a textura (bilinear) |

Não há imagem própria para a faixa do título, a barra das abas nem a barra de ajuda: são as faixas do
painel, e as abas são textos (fonte 2) com as cores do arquivo (a aba escolhida mais apagada). Peças de
`03000042` desenhadas mas não vistas nessas telas (as opções do menu principal ainda não abrem): o rádio
[0, 16, 16, 16], a barra de volume [0, 32, 64, 8] (segmentos do pêssego ao laranja) e as setas grandes
[0, 40, 24, 24] e [40, 40, 24, 24]; têm substituta (`menu_radio`, `menu_volume`, `menu_bigarrow_*`), sem
conferência na tela. O resto do que aparece por cima dessas telas é do HUD (`0300007a`, `03000128`,
`03000026`, `0300005a`/`03000002` da cortina de sangue, `0300018e`).

Os símbolos (`specialchar`, 0x002dd8d8, 17 entradas de `03000075`; a mesma folha também tem os botões do
Xbox e do GameCube, que o jogo do PS2 não usa):

| Código | Botão do PS2 | Recorte | Código | Botão do PS2 | Recorte |
|---|---|---|---|---|---|
| `~A` | X (cruz) | [235, 1, 19, 19] | `~L` | L1 | [213, 93, 22, 13] |
| `~B` | triângulo | [235, 21, 19, 19] | `~R` | R1 | [190, 93, 22, 13] |
| `~X` | círculo | [235, 41, 19, 19] | `~C` | L2 | [213, 111, 22, 17] |
| `~Y` | quadrado | [235, 61, 19, 19] | `~D` | R2 | [190, 111, 22, 17] |
| `~V` | direcional cima/baixo | [236, 100, 18, 19] | `~F` | analógico esquerdo | [215, 72, 18, 18] |
| `~H` | direcional esquerda/direita | [236, 80, 18, 19] | `~E` | analógico direito | [192, 72, 18, 18] |
| `~W` | direcional inteiro | [167, 90, 20, 20] | `~S` | Start | [162, 113, 26, 13] |
| `~T` | Select | [150, 109, 9, 7] | `~J` / `~I` | visto / ponto dos objetivos | [176, 57, 16, 12] / [198, 59, 7, 7] |

Duas esquisitices conferidas:

- Os quadradinhos (▪▪▪▪) ao lado de RESTART/REPLAY na pausa e no fim de missão e do "60" na aba VIDEO das opções
  não são 2D: é a placa verde de saída de emergência (setas e o homem correndo) na parede do fundo de
  07000012, vista pelo vidro translúcido do painel. Aparecem no mesmo ponto da tela em todos os menus porque
  a câmera é a mesma; sem menu, a placa está lá (`--wait 2` sem `--input`). Comportamento do jogo.
- A silhueta com bolhas coloridas (`030000db`, alfa 0x50, aditiva) é o desenho da textura: luzes de festa
  desfocadas e um homem de smoking erguendo uma taça. A textura é de 8 bits com paleta de 256 cores CSM1; com
  a paleta reordenada como o port faz (as faixas 8-15 e 16-23 de cada grupo de 32 trocadas, a mesma regra do
  hash de paleta que acha 89% das texturas do pacote do PCSX2) as bolhas são manchas suaves (diferença média
  entre vizinhos 31), e na ordem crua viram ruído de cores trocadas (72). O recorte é a textura inteira
  [0, 0, 127, 127]. Então não é erro de paleta nem de formato: é o jogo (aditiva com alfa 0x28 de 0x80, ela só
  clareia o vidro).

### Molduras em nove pedaços

O painel é um rótulo de imagem: o jogo estica a textura inteira no retângulo do controle. As páginas do jogo
usam o tamanho para o qual o painel foi desenhado, mas uma página maior (a de vídeo do PC, 15 linhas)
esticava os cantos arredondados e as faixas na vertical. Regra (`psi_2d_nine_slice`, `platform/psi_2d.h`):

- Uma textura da tabela de molduras (`psi_2d_find_frame`; por enquanto o painel `03000070`, cortes de 64, 72,
  64 e 80 texels da esquerda, de cima, da direita e de baixo, nos trechos retos da moldura e do vidro) mostrada
  inteira num quadrado é medida em pixels da janela por texel, em x e em y (na área 4:3 o pixel da tela do PS2
  é 7/6 mais largo que alto).
- Se a razão x / y fica a até 10% da proporção de referência da moldura (1: texels quadrados), o quadrado é
  desenhado como sempre. As páginas do jogo (440 x 248 na tela do PS2: razão 1,033) ficam idênticas pixel a
  pixel no 2D original (diferença zero contra o commit anterior em 1024x576 e 3840x2160: pausa, fim de
  missão, resultado, central, controles e teclas).
- Fora disso vira nove pedaços: os quatro cantos com a escala do eixo menos esticado nos dois eixos (texels na
  proporção de referência; na página de vídeo, a mesma escala da pausa), as bordas de cima e de baixo esticadas
  só na horizontal, as dos lados só na vertical e o meio nos dois. Um quadrado espelhado troca os cortes de
  lado; um quadrado da largura inteira da tela (esticado até a janela) não é cortado.
- Vale no 2D original (`add_2d` em `runtime_gfx.cpp`) e na camada HD (`ui_hd_build`): lá os nove pedaços usam
  uma só rasterização da substituta, na escala dos cantos (os cantos saem 1:1, as bordas e o meio esticam um
  desenho que não muda nessa direção).

As peças de componente (as listas, as barras, as caixas de marcar) não precisam disso: o próprio
`Component_SetupInstance` já põe as peças fixas nas bordas e estica só as do meio.

### Símbolos de botão por aparelho

Na camada HD, cada símbolo `~X` de um texto vira o desenho do aparelho em uso (`[ui] prompts` no
`nightfire.ini`, ou `--prompts`):

- `ps2`: os símbolos do PS2 em vetor (`prompts.ps2` no `map.json`).
- `keyboard`: uma tecla (retângulo arredondado escuro com borda clara, desenhado pelo Direct2D) com o nome da
  tecla ligada ao comando (`platform/input_bindings.h`, religável na página de teclas): no menu, X = escolher
  (Espaço), círculo = Tab, triângulo = voltar (Backspace), quadrado = `menu_square` (E), Start
  = pausa (Enter), direcional = as setas do menu (↑↓, ←→; outras teclas aparecem como "W/S"). No mapa de
  botões da aba CONTROLS da pausa (as células da lista 10 de 0x10000028, `Menu_DisplayControllerStyleList`), e
  sempre para L1/L2/R1/R2, os analógicos e o Select, o símbolo mostra a tecla do comando do jogo que aquele
  botão faz no estilo escolhido (`psi_pad_style_inputs`): no estilo NightFire, X agachar = C, L1 mirar =
  Mouse 2, R1 atirar = Mouse 1, analógico esquerdo = WASD, direito = Mouse. Uma tecla mais larga que o símbolo
  empurra o resto da linha, e o bloco volta ao alinhamento do formato (centrado: metade para cada lado). Sem
  tecla ligada, fica o símbolo do PS2.
- `xbox`: os botões do controle do Xbox que fazem o mesmo no PC (`platform/psi_pad.h`): X = A, círculo = B,
  quadrado = X, triângulo = Y, Start = Menu, Select = View, L3/R3 = LS/RS, direcional = direcional; com
  `[controls] pad_shoulders = triggers` (padrão) L1/R1 = LT/RT e L2/R2 = LB/RB, com `bumpers` o contrário.
- `auto` (padrão): o último aparelho usado. O `main.cpp` chama `UiHd_SetPromptDevice` a cada quadro com
  `psiInput_ActiveDevice()` (teclado e mouse, ou o controle); outro módulo pode chamar o gancho também.

O visto e o ponto dos objetivos (`~J`, `~I`) não são botões: ficam sempre com o desenho próprio. No 2D
original os símbolos continuam os do jogo (a textura `03000075`). O relatório da captura tem
`simbolos <escolhido>/<em vigor>` na linha `ui_hd`.

## Opções do PC no menu

Só do port: o PS2 não tem nenhuma destas opções. Ficam dentro do próprio menu de pausa do jogo e são
gravadas no `nightfire.ini`. Há dois estilos (`[ui] menu_style`, `--menu-style`, a linha **Menu style** em
INTERFACE): **Modern**, o padrão, descrito em "Menus no estilo Modern" abaixo e em `docs/design.md`, e
**Original**, a página em abas com as peças do jogo descrita nesta seção.

Como chegar: Enter abre a pausa; a lista principal (CONTINUE, RESTART, QUIT) ganha uma quarta linha,
**OPTIONS**; escolhê-la (Espaço, ou o clique) abre a página de opções (0x40000060), uma página só, montada
como a pausa, com abas: **VIDEO**, **GRAPHICS**, **INTERFACE**, **CONTROLS** e **KEYS** (e **AUDIO**, com as
barras de volume do jogo apagadas e "em breve", quando os seis nomes cabem na faixa; na fonte 2 do jogo não
cabem, então ela fica de fora). Backspace (ou o botão direito) volta à pausa; Enter fecha o menu inteiro.

Navegação: o foco começa na faixa de abas; esquerda/direita troca de aba na hora (como as abas da pausa) e
baixo desce para as linhas; cima/baixo anda pelas linhas (de cima da primeira, sobe para a aba);
esquerda/direita mexem no controle da linha (a barra, a escolha); Espaço numa escolha avança. Com o mouse:
ver "Mouse nos menus". A página volta na última aba aberta.

O visual e os controles são os do jogo: o painel `0x03000070` da pausa no tamanho do arquivo (550 x 266, sem
esticar), a faixa de abas como MISSION/OBJECTIVES/CONTROLS/SCORE (botões do conjunto 2, só o texto na fonte
2, escuros 0x201707ff e o da aba aberta ou com o foco claro 0x645a49ff), a barra de ajuda com os símbolos de
botão (`~H Tab ~V Options ~B Back` na faixa; `~A Select ~V Scroll ~H Change ~B Back` nas linhas; com a camada
HD eles seguem o aparelho) e, em cada aba, linhas como as das páginas de opções do menu principal ("AV
Options", "Advanced Options" do `08000002`): o nome à esquerda num rótulo da fonte 3 que acende com o foco
da linha e o controle à direita:

- **barra** (valores contínuos: campo de visão, sensibilidades, zona morta): a barra de volume do "AV
  Options", uma barra de rolagem proporcional (o preenchimento laranja vai até o valor; as setas aparecem
  com o foco). O `08000001` das fases não tem essa barra: a página cria um conjunto de componentes (0x50)
  com as peças do conjunto 10 do `08000002` (`MenuPc_EnsureComponents`, todas da textura 0x03000042). O
  valor também vai no nome ("Field of view  60");
- **escolha** (resolução, modo de tela, escala, anti-aliasing, filtro, posição do HUD, idioma) e
  **ligado/desligado** (VSync, texturas HD, interface HD, inverter Y, vibração, gatilhos): o rádio do
  conjunto 3, o texto da opção centrado e as setas esquerda/direita com o foco, como "Subtitles" e
  "Vibration" do jogo;
- **botão** ("Apply changes"), só o texto, na coluna dos nomes (como "Restore Defaults");
- **lista** (a de teclas), a lista com moldura e barra de setas do conjunto 3, como as listas do menu
  principal.

Cabem 7 linhas; numa aba com mais (CONTROLS tem 9) as linhas rolam com o foco ou com a roda do mouse sobre
os nomes, e uma barra do conjunto 5 (só mostra) diz onde se está. A página é montada com as estruturas do
sistema de menus portado (`src/engine/menu_pc_options.cpp`: `Page_Init` pela mensagem 0x43 do gerente,
controles `NEW_CONTROL` pela 0x42, tratadores com `Menu_PortRegisterHandler`; as abas com
`MenuPc_RegisterTab` e as linhas com `MenuPc_AddRow`) na primeira vez que é aberta, e desenhada pela camada
2D (câmera 7), então a camada HD (F4) também a redesenha. A linha nova da pausa entra por um embrulho dos
tratadores do jogo (`P_PAUSE_Handler`, `C_GCPAUSE_Handler`), que chama o tratador anterior para todo o
resto: as entradas originais não mudam. A lista principal cresce uma linha (a altura dela é de três linhas
no arquivo). Voltando da página do PC, a pausa fica como estava (o `0x4c` do jogo, que monta as listas de
novo, não roda). Ids: a página 0x40000060, controles 0x10000300-0x100003ff (as abas a partir de 0x10000310, as
linhas de 0x10000320; 0x10000400 a 0x100004ff são das abas de controles e teclas).

Duas peculiaridades do sistema de menus que a página contorna: `Scroll_Update` chama `Scroll_Init` a cada
tick, e ela zera o estado da barra (uma barra apagada voltaria a receber o foco e o mouse: a página põe o
estado 2 de novo a cada tick); `Label_Update`, no primeiro update sem foco, cria um processo 0xe1 que
desbota em 15 ticks até a cor sem foco daquele momento, e os processos rodam mesmo com o controle escondido
(as cores vão antes do texto e, quando a cor sem foco muda depois, o processo em curso é apagado).

VIDEO, GRAPHICS e INTERFACE editam uma cópia da configuração; "Apply changes" (em cada uma das três; apagada
e pulada quando não há nada a aplicar) aplica e grava, e sair da página descarta o que não foi aplicado.

| Opção | Valores | Como funciona | Chave |
|-------|---------|---------------|-------|
| VIDEO: Resolução | os modos do monitor (DXGI) mais a atual | na janela, o tamanho da área de desenho (só os que cabem no monitor; a janela fica centrada); na tela cheia exclusiva, o modo do monitor; sem bordas, fixa (a do monitor) | `[video] width`, `height` |
| Modo de tela | Janela, Janela sem bordas, Tela cheia exclusiva | a janela sem bordas é a de antes (`--fullscreen`, Alt+Enter); a exclusiva usa `SetFullscreenState` e `ResizeTarget` do DXGI (a cadeia de troca tem `DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH`), sai sozinha quando a janela perde o foco e volta quando ele volta | `window_mode = windowed\|borderless\|exclusive` |
| Sincronia vertical | Ligado, Desligado | `Present(1)` ou `Present(0)` | `vsync` |
| GRAPHICS: Escala da renderização | 50, 75, 100, 125, 150, 200% | a cena 3D num alvo próprio desse tamanho (no máximo 16384 de lado) e reduzida para a janela por um shader que faz a média de até 4 x 4 amostras bilineares sobre a área de cada pixel (200% = 4 amostras por pixel, supersampling); abaixo de 100%, ampliada; o 2D (HUD, menus, legendas, camada HD) é desenhado depois, na resolução da janela | `render_scale` |
| Anti-aliasing | Desligado, MSAA 2x, 4x, 8x (os que a placa aceita) | a cena num alvo multiamostrado, resolvido (`ResolveSubresource`) antes da redução e do 2D; a névoa roda por amostra (ver abaixo) | `msaa` |
| Filtro de texturas | Bilinear, Trilinear, Anisotrópico 2x a 16x | o sampler das texturas da cena (o 2D tem os seus) | `texture_filter = bilinear\|trilinear\|aniso2\|aniso4\|aniso8\|aniso16` |
| VIDEO: Campo de visão vertical (barra) | 40 a 100 graus, de 5 em 5 | o `--fov`: vertical, o horizontal cresce com a largura (Hor+) | `fov` |
| Texturas HD | Ligado, Desligado | a tecla T (o pacote do PCSX2) | `hd_textures` |
| INTERFACE: Interface HD | Ligado, Desligado | a tecla F4 (a camada 2D em alta definição) | `[ui] hd` |
| Posição do HUD | Centro (original), Bordas da tela | a âncora da camada HD (`--hd-ui-anchor`) | `[ui] anchor = centre\|edges` |
| Idioma das opções | English (padrão), Português | o idioma dos textos só do port | `[ui] language_pc = pt\|en` |
| INTERFACE: Legendas | Ligado, Desligado | a opção do próprio jogo (`DrawInfo[2]`, a "Subtitles" da AV Options): vale na hora, sem o Apply | `[ui] subtitles` |
| GRAPHICS: DLSS / DLAA | "Em breve" | apagada e pulada pela seleção (pendência 8 do CLAUDE.md) | |

Escala, MSAA e névoa: com escala diferente de 100% ou com MSAA, o quadro tem duas passadas: os itens 3D (na
ordem do jogo) no alvo da cena, com a profundidade da cena, a resolução do MSAA e a redução para o back
buffer, e depois os itens 2D (na ordem do jogo) por cima, no tamanho da janela, sem profundidade. A passada
de névoa (`psiDrawFogPass`) lê a profundidade: com MSAA ela é `Texture2DMS`, e o pixel shader da névoa
recebe `SV_SampleIndex`, então roda por amostra e mistura a névoa de cada amostra pela profundidade dela
(antes da resolução), o que mantém a névoa igual e as bordas suaves. Em 100% sem MSAA o renderizador faz o
caminho de antes, numa passada só, e as imagens são idênticas às do commit anterior. Sem memória para o tamanho
pedido, o renderizador cai para sem MSAA e depois para 100% (a página mostra o que ficou valendo).

Alpha-to-coverage (só do PC, com MSAA): o teste de alfa do GS descarta o pixel inteiro, então o MSAA não suavizava as
bordas recortadas por ele. Agora os desenhos opacos que o teste recorta vão com o alfa para a cobertura
(`AlphaToCoverageEnable` num blend state próprio e o pixel shader `ps_a2c`): a escolha é
`gs_alpha_coverage_candidate` (`data/map_item.h`): um teste GEQUAL/GREATER com limiar sem mistura, ou com a mistura de
transparência `(Cs - Cd) * As + Cd` e um teste que só deixa passar o alfa cheio, onde a mistura dá Cs. Nos dados, quase
toda a geometria das fases usa ALPHA 0x44 com TEST 0x5001b (GEQUAL 1: misturada, as bordas já são suaves) e fica como
está; o caso que serrilhava é o TEST 0x507fd (GREATER 0x7f: só o alfa cheio passa), 330 partes de malha, com objetos
colocados em 11 fases (o bambu da abertura de 07000001, as plantas de 0700000a e 07000012, 33 objetos em 07000002). O alfa de saída é a cobertura afiada em volta
do limiar pela derivada de tela do alfa (`fwidth`): `saturate((a - limiar) / fwidth(a) + b)`, com b = 0,5 (a rampa
centrada) e b = 1 quando o limiar é o topo do alfa (o 0x7f: o alfa nunca passa dele, e a rampa centrada daria metade da
cobertura a toda folha fina, pontilhada). Com b = 1 todo pixel que passa em 1x fica inteiro e a rampa de um pixel vai
para fora: as bordas e as pontas finas das folhas (que em 1x se quebram em pontos) ficam contínuas e suaves; a cor é a de
`ps_main` e o alfa gravado no alvo é a cobertura (1 dentro: o alfa das folhas é cheio). Sem MSAA nada muda (as capturas
são idênticas às de antes), e com MSAA e `--no-alpha-coverage` também. A linha `video` do relatório e a do `--bench`
dizem `cobertura ligada/partes` (as partes desenhadas assim no último quadro). Custo no Radeon integrado, 1920x1080, MSAA
4x, fora da tela, 8 s (com/sem): a abertura de 07000001 com o bambu 307,8/305,6 e 300,6/292,2 quadros por segundo,
07000012 286,2/285,9 e 288,0/288,1, 0700000a 320,4/320,0 e 319,8/320,4: dentro do ruído.

Confirmação: mudar a resolução ou o modo de tela e aplicar abre a caixa de mensagem do próprio jogo
(`Menu_CreateOptionBox`, página 0x4000002f) com "Manter estas configurações de vídeo?", SIM/NÃO e a
contagem regressiva de 15 s (ticks de 60 Hz); SIM grava, NÃO, voltar ou o fim do tempo volta à configuração
anterior inteira (sem gravar). Fechar o menu (Enter) no meio da contagem também volta. As outras mudanças
valem e são gravadas na hora em que se aplica.

Configuração (`platform/settings.h`, `platform/video_settings.h`): o `nightfire.ini` fica ao lado do
executável (ou `--config <ini>`), com comentários, ordem e chaves desconhecidas mantidos ao regravar. As
chaves acima ficam em `[video]` e `[ui]` (a `[video] fullscreen` antiga ainda é lida: `1` vira
`window_mode = borderless`; ao gravar, dá lugar a `window_mode`); `[controls]` e `[bindings]` são da página
de controles; `[game]` e `[ui] subtitles` são as opções do próprio jogo (ver "Opções do codinome"). Na partida o arquivo é a base e as opções da linha de comando valem por cima (sem serem
gravadas, a não ser que o jogador aplique na página). Com `--screenshot`, o arquivo só é lido e gravado com
`--config`, então as capturas não dependem nem mexem nas opções do jogador. Exemplo:

```ini
[video]
width = 2560
height = 1080
window_mode = borderless
vsync = 1
render_scale = 150
msaa = 4
texture_filter = aniso16
fov = 65
hd_textures = 1

[ui]
hd = 1
anchor = edges
language_pc = en
```

Textos: os rótulos são uma tabela só do port em inglês (padrão, como o texto do jogo) e português
(`src/engine/pc_text.h`, `PcText`), pelo `[ui] language_pc` (um dia podem seguir o idioma do jogo). As fontes do jogo só têm os acentos que os
idiomas do disco usam (faltam Ã, Õ, Ç, Ô, ã, õ...; o `__Font_DrawText` desenharia o glifo anterior no lugar),
então no 2D original cada caractere sem glifo vira a letra sem acento ("OPCOES", "Resoluçao"); com a camada
HD ligada o texto mantém todos os caracteres e sai certo ("OPÇÕES", "Resolução"), porque ela redesenha a
string com o DirectWrite.

As abas de controles e de teclas (ver "Controles (teclado, mouse e controle)") são de outro módulo e se
registram com `MenuPc_RegisterTab` (construtor, entrada, tick, saída, texto de ajuda); o construtor cria as
linhas com `MenuPc_AddRow`.

Conferência (capturas em `out/shots/pcopt`, 1024x576, 07000012): a pausa com a linha nova; cada aba (`--open-menu-page
40000060 --menu-tab video|graphics|interface|controls|keys`), também em 768x576 e em 3840x2160 com `--hd-ui
--prompts keyboard`; a caixa de confirmação com a contagem; a mesma página em inglês e com a camada HD; 200% e MSAA
4x contra 100% (bordas suaves, o 2D igual); a névoa de 0700000c igual com MSAA 4x e com 150% + MSAA 8x;
anisotrópico 16x contra bilinear num piso em ângulo raso; o campo de visão mudado pelo menu, gravado num
`--config` e restaurado na partida seguinte (e `--fov` por cima do arquivo).

## Menus no estilo Modern

O estilo de menu padrão do port: um sistema de desenho próprio (`docs/design.md`: tokens, tipografia, grade,
componentes, movimento, as telas), inspirado nos menus de *007 First Light*, desenhado com Direct2D e DirectWrite
na resolução da janela, sempre (não depende da camada HD, F4). Cobre a **pausa** (com as opções do PC numa aba),
o **fim de missão**, o **resultado**, o **menu principal** (o caminho de um jogador, com o **dossiê** e os
**créditos**: ver "O menu principal no estilo Modern", abaixo) e o **aviso de controle desconectado**; as opções de
multijogador do codinome, o ajuste de tela e o controle das missões de carro continuam as páginas do jogo, no estilo
Original. `--menu-style
original` (ou a linha **Menu style** em INTERFACE, que vale na próxima vez que um menu abre) volta aos menus do
jogo como foram portados, iguais pixel a pixel. Sem Direct2D, Original.

### Os menus continuam os do jogo

O jogo abre os menus como sempre: `Player_Update` a pausa (menu 0x80000002, página 0x4000004b, status 3),
`Mission_Update` o fim de missão (0x80000004 / 0x40000042, status 4) e o menu principal 07000048 o resultado
(0x40000036, status 5, com as estatísticas 0x40000037 e o bônus 0x40000038); os tratadores dele
(`P_PAUSE_Handler`, `C_GCPAUSE_Handler`, `P_ENDMISSION_Handler`, `P_NFRESULTS_Handler`...) decidem o que cada
escolha faz. As telas Modern só trocam o desenho, pela ponte `engine/menu_modern.h`:

- **Leitura.** A cada quadro de vídeo, `ModernUiApp` lê `MenuModern_Snapshot()` (o gerente 0: a página, a de baixo
  quando a caixa de mensagem está por cima, e o que cada tela mostra, em UTF-8) e abre a tela que o menu pede (só
  com o estilo Modern em vigor) ou a fecha quando ele some, também quando o jogo o fecha sozinho (o aviso de
  controle desconectado, um recomeço). O que vem de onde: as linhas das listas (as letras espaçadas do arquivo,
  "C O N T I N U E", viram "CONTINUE"), os rótulos e os botões das abas; a confirmação aberta (a lista tag 0xd da
  pausa com a pergunta da tag 0xc, a caixa de mensagem 0x4000002f ou a caixa do cartão 0x4000002a do menu
  principal, a de gravar o perfil depois do resultado); os objetivos pelo módulo `Mission_*` (os
  visíveis na ordem de `Mission_ObjectiveState`, a da lista da pausa, e depois os ainda escondidos, estado 0 ou 1);
  o mapa de botões que `Menu_DisplayControllerStyleList` monta na lista tag 10 ("~A Crouch"...); o nome e a
  descrição da missão de `sp_level` (+4 e +8, os rótulos da tabela 3 do `USATxt.dat`), o nível dentro dela
  (`MissionData`), a dificuldade (`GameState` +0x28: 1 Operative, 2 Agent, 3 00 Agent); o motivo da falha
  (`Mission_FailLabel`, "Killed In Action" do jogo na morte, ou o objetivo fracassado); a pontuação, a meta e a
  medalha que o `P_NFRESULTS_Handler` escreveu, e as linhas das estatísticas.
- **Escrita.** `MenuModern_Request` põe o pedido numa fila que o gancho `Menu_SetPortTick` (o único do núcleo dos
  menus além do mouse; posto pelo `ModernUiApp`, sem efeito com a fila vazia, como sempre no estilo Original) executa no começo do tick do gerente, logo depois do mouse: `Tab`
  põe o cursor do gerente no botão da aba (o `Page_Update` muda o foco e manda 0x4f/0x4e, como o direcional),
  `Pick` escolhe a linha de uma lista (0x1d, como cima/baixo: o 0x49 vai ao dono) e `Press` aperta um botão do
  controle do PS2 por um tick (`Input_SetAction`: Start 0x1e, X 0x1f, quadrado 0x20, círculo 0x21, triângulo
  0x22), que os updates dos controles leem como um botão de verdade (o som, o 0x4b/0x5d/0x5e ao dono, o 0x5f do
  gerente, o `MenuManager_Monitor` fechando com o Start). Um `Tab` ou um `Press` encerra o tick: o pedido seguinte
  espera o efeito dele; a tela não aceita comandos enquanto a fila não esvazia.

| Na tela Modern | No jogo |
|----------------|---------|
| RESUME | a linha 0 da lista tag 6 e o X: `C_GCPAUSE_Handler` fecha o menu, que despausa (ação de fechar 0x1e) |
| Start, Enter, voltar no PAUSE MENU | o Start: o `MenuManager_Monitor` fecha a pausa e despausa |
| RESTART MISSION, QUIT MISSION | a linha 1 ou 2 e o X: o jogo mostra a pergunta e o sim/não com o "não" escolhido, desenhados como o `Dialog` do kit; SIM e NÃO são a linha da lista 0xd e o X, voltar é o "não" |
| abas PAUSE MENU, OBJECTIVES, CONTROLS, SCORE | o foco no botão da aba 0, 1, 3, 4: o `C_GCPAUSE_Handler` monta o painel (na SCORE, com um jogador, as estatísticas da missão do `PlrStat_GetScore(1)` na lista tag 8) |
| o esquema de botões | a linha da lista tag 9 (0x49: `Menu_ChangeControllerStyle` e o mapa de botões) |
| o eixo Y | o círculo (0x5e da lista tag 9) |
| REPLAY, QUIT (fim de missão) | a linha da lista 0x100001a4 e o X: `P_ENDMISSION_Handler` lê o valor dela |
| CONTINUE, STATS (resultado) | o X ou o círculo no botão 0x10000165: `P_NFRESULTS_Handler` (0x4b, 0x5e) |
| BACK (estatísticas) | o triângulo: o 0x5f do gerente desempilha o resultado |
| a caixa de mensagem do jogo | o `Dialog`: a linha da lista 0x10000120 e o X; voltar é o triângulo |
| a caixa do cartão (gravar o perfil depois do resultado) | o `Dialog`: a linha da lista 0x100000fc e o X |

Nenhum caso precisou chamar as funções dos tratadores direto. Só do port: OPTIONS (a linha e a aba) abre a tela de
opções embutida, sem pedido ao jogo. O DOSSIER (a linha e a aba) é o quadrado: o jogo mostra a página 0x4000002b, que
é o dossiê Modern (uma vista do menu principal Modern: a tela do resultado fecha; voltar do dossiê reabre o resultado).
Das estatísticas o jogo não abre o dossiê (`P_NFSTATS_Handler` não trata o quadrado): a aba mostra um aviso.

**O que some do quadro.** `RuntimeGfx::set_hidden_2d`: com a pausa e o resultado, todo o 2D do jogo (o menu e o
HUD); com o fim de missão pela morte, só o do menu (a câmera 7): a cortina de sangue do HUD (o que ele ainda
mostra) fica, desenhada nítida sobre a cena desfocada (`Renderer::render_2d`, depois de `blur_background`) e por
baixo do véu e do texto (a cortina escurece a cena: precisa dela clara por trás); num fracasso por objetivo (o
jogador vivo, o HUD com a vida e a munição), todo o 2D some como na pausa.

### As telas

- **A pausa** (`src/ui/modern_pause.h`): a barra de cima PAUSE MENU · OBJECTIVES · CONTROLS · SCORE · OPTIONS com Q/E
  (LB/RB); SCORE vale também com um jogador, como na pausa do jogo: a tabela que o jogo monta (Category / Current /
  Target: os momentos Bond, os inimigos abatidos, desacordados e rendidos, a precisão, a vida, o tempo e as fichas
  007, cada um com a meta da tabela de pontuação), só para ler, com a missão e a dificuldade à esquerda. PAUSE MENU: a lista grande RESUME, RESTART MISSION, QUIT MISSION e,
  menor, OPTIONS, como o menu principal da referência, e à direita a missão (nome, descrição, objetivo atual, nível,
  dificuldade, o medidor dos objetivos cumpridos). OBJECTIVES: o contador, os objetivos com a situação (cumprido,
  fracassado, em andamento, confidencial) e a dica do jogo do que tem o foco. CONTROLS: o aparelho, o esquema e o
  eixo Y do jogo e o mapa de botões do esquema com os símbolos do aparelho (os glifos do Xbox pelo esquema dos
  ombros, `ui_hd_xbox_name`, ou as teclas ligadas ao comando que o botão faz, `ui_hd_prompt_keys`). OPTIONS: a tela
  de opções (abaixo), embutida (`OptionsScreen::set_embedded`: o fundo e a barra de cima são da pausa; Q/E e voltar
  das categorias viram pedidos para ela).
- **O fim de missão** (`src/ui/modern_end.h`, `FailScreen`): "MISSION FAILED" em tela cheia, devagar (o véu em
  0,9 s, o título com o espaço entre as letras abrindo em 1,8 s, as linhas finas crescendo dos lados), o motivo do
  jogo e as opções dele lado a lado (REPLAY, QUIT), que só respondem depois de aparecerem (0,8 s).
- **O resultado** (`ResultsScreen`): o fundo opaco (o menu principal não tem cena), RESULTS · STATS · DOSSIER em
  cima seguindo a página do jogo, "MISSION COMPLETE", a missão, a pontuação, a meta da medalha e a medalha que o
  jogo calculou (`PlrStat_GetScore`, ver docs/engine.md, "Estatísticas e pontuação"), CONTINUE / STATS / DOSSIER;
  as estatísticas com as linhas da lista do jogo (alvo, valor e pontos de cada categoria, a soma, o bônus da
  dificuldade e o total) ou, antes de o jogo montá-la, as categorias dele com "—". O "continuar" segue o
  caminho do jogo até o mapa das missões (Original), com a próxima missão liberada.

### As opções

A aba OPTIONS da pausa: à esquerda, "OPTIONS /" e as categorias **VIDEO**, **GRAPHICS**, **INTERFACE**, **GAMEPLAY**,
**AUDIO**, **CONTROLS**, **KEY BINDINGS** e **CONTROLLER**; no meio, as linhas com seções; à direita, a descrição da opção com o foco (título, texto, "Current
value", e nas categorias de vídeo um medidor da resolução da cena 3D); embaixo, o port e o jogo à esquerda e os
comandos à direita (tecla ou glifo do Xbox, conforme o aparelho em uso).

| Categoria | Linhas |
|-----------|--------|
| VIDEO | DISPLAY: Resolution (lista), Display mode (lista), VSync (chave); VIEW: Field of view (barra 40-100); APPLY CHANGES |
| GRAPHICS | IMAGE QUALITY: Render scale, Anti-aliasing, Texture filtering (listas); TEXTURES: HD textures (chave); UPSCALING: DLSS / DLAA (travada); APPLY CHANGES |
| INTERFACE | HUD: HD interface (chave), HUD position (lista); ON-SCREEN TEXT: Subtitles (chave, a do jogo); MENUS: Menu style, Options language (listas); APPLY CHANGES |
| GAMEPLAY | AIMING: Auto aim, Crosshairs (chaves), Manual aim (Toggle/Hold); MOVEMENT: Crouch (Toggle/Hold); ITEMS & HUD: Weapon auto switch, Flashing objects, HUD always on (chaves) |
| AUDIO | VOLUME: Music volume, Effects volume (barras de 0 a 100%, de 5 em 5) |
| CONTROLS | INPUT: Active device (informação); MOUSE: Mouse sensitivity (barra logarítmica, com a volta em cm a 800 e 1600 DPI no medidor), Aiming sensitivity (barra), Invert mouse Y (chave) |
| KEY BINDINGS | a tabela ACTION / PRIMARY / SECONDARY / CONTROLLER e RESTORE DEFAULT BINDINGS |
| CONTROLLER | STICKS: Look sensitivity, Dead zone (barras), Invert controller Y (chave); BUTTONS: Button layout (os 8 esquemas do jogo), Fire with triggers, Vibration (chaves) |

O mouse fica em CONTROLS (o jogador procurou a sensibilidade lá, 10/10/2026), o controle em CONTROLLER e as teclas em
KEY BINDINGS (a antiga MOUSE & KEYBOARD; `--menu-tab keys` ou `bindings`, `mouse` abre CONTROLS): nenhuma opção aparece
em duas categorias (`modern_options_controls_has_the_mouse_and_no_duplicates`). R em CONTROLS restaura o mouse (as
duas sensibilidades e a inversão); em CONTROLLER, também a inversão do controle.

As opções e o comportamento são os da página Original (as mesmas peças por baixo): VIDEO, GRAPHICS e
INTERFACE editam uma cópia pendente aplicada com APPLY CHANGES ou **F** (Y no controle; a descrição avisa
quando há mudanças pendentes; sair das opções descarta); resolução e modo de tela abrem o diálogo "Keep these
settings?" com a contagem de 15 s (sem resposta, a anterior volta); o resto vale na hora e vai para o
`nightfire.ini`. **R** (X no controle) restaura os padrões da categoria: nas pendentes, na cópia; nas que valem na
hora, depois de um diálogo de confirmação. Na tabela de teclas cada célula religa só aquela entrada: PRIMARY e
SECONDARY as duas do teclado/mouse (`nf::bind_slot`, com os conflitos de `nf::bind`), CONTROLLER a do controle
(entre parênteses, o que o esquema de botões do jogo faz sem nada na tabela); Esc cancela, Delete limpa a célula.
No estilo Original a linha OPTIONS da pausa do jogo abre a página Original em abas (`MenuPc_SetOptionsHook`: o
gancho só abre a aba Modern com a pausa Modern aberta).

As opções do próprio jogo (decidido com o usuário: no Modern as páginas de opções do PS2 não aparecem como tais; as
úteis entram aqui e as obsoletas no PC, widescreen, ajuste de tela, tela dividida e modo do som, ficam só nas páginas do
jogo no estilo Original) moram no lugar do jogo e são lidas e escritas por `nf::game_option`/`set_game_option`
(`platform/game_options.h`), valendo na hora, também na missão em andamento: o jogo lê cada byte a cada tick (a mira
desligada some do HUD no tick seguinte, sem refazer o HUD).

| Linha | Onde mora | O que faz no jogo |
|-------|-----------|-------------------|
| GAMEPLAY: Auto aim | `PlayerSetting[0]` +1 (`autoaim`) | `Player_AutoAim` mira sozinho perto do centro da tela |
| Crosshairs | +8 | a mira aparece sem mirar (`HUD` da mira); desligada, só mirando |
| Manual aim | +3 (Toggle 1, Hold 0) | o botão de mirar alterna ou precisa ficar apertado |
| Crouch | +4 (Toggle 1, Hold 0) | o botão de agachar alterna ou precisa ficar apertado |
| Weapon auto switch | +0xa | pegar uma arma melhor já a põe na mão |
| Flashing objects | +0xc (On 2, Off 0) | os itens pulsam a menos de 4 m da câmera |
| HUD always on | +0xb (On 0, Off 1) | desligado, o HUD some quando o jogador não está levando dano |
| INTERFACE: Subtitles | `DrawInfo[2]` | as legendas das falas |
| AUDIO: Music volume, Effects volume | `SFXMusicSetVolume`/`SFXSetVolume` (`MusicMasterVolume` 70, `SfxMasterVolume` 100) | o volume da música e dos efeitos, ouvido na hora (ver "Som") |
| CONTROLLER: Vibration | +9 | a mesma "Vibration" do jogo |

A descrição de cada uma é o texto de ajuda do jogo para o valor em vigor (a `help_table` que `Page_SetHelpText` usa nas
páginas do jogo; em português, uma tradução). O `nightfire.ini` segue a regra de "Opções do codinome" (só a opção
mudada vai para o arquivo). **R** restaura: GAMEPLAY com os valores do perfil padrão do jogo (`def_codename`), AUDIO
com os do "Restore Defaults" da AV Options (70 e 100), as legendas de INTERFACE pela regra dessa página (desligadas
na versão americana) e o esquema de botões de CONTROLLER com o de `def_codename` (7, Classic Bond).

Deep link (para o menu principal Modern abrir GAMEPLAY, CONTROLLER e AUDIO pelo menu do codinome):
`OptionsScreen::open_at(categoria, back_leaves = true)` abre direto na categoria, com o foco na primeira linha; com
`back_leaves`, voltar das linhas sai da tela (`request_close`, ou `take_back_request()` quando embutida) em vez de ir à
coluna das categorias, que continua lá para o mouse. Fora da pausa a tela anfitriã usa `set_embedded(true)`, desenha o
fundo e a barra de cima e chama `frame`/`build` a cada quadro. Os nomes das categorias (`parse_cat`/`cat_name`) são os
do `--menu-tab`: video, graphics, interface, gameplay, audio, controls, keys, controller.

### Entrada

Teclado: setas/WASD, Espaço, Esc/Backspace, Q/E (os comandos do menu da tabela de teclas, mais Enter e Esc
fixos); setas seguradas repetem. Na pausa, o Start fecha (a tecla da pausa, Enter por padrão, e o Start do
controle, como no jogo) e Espaço escolhe; voltar no PAUSE MENU fecha também, nas outras abas volta ao PAUSE MENU.
Nas opções, no diálogo, no fim de missão e no resultado (onde o Start do jogo também escolhe), Enter escolhe. Mouse:
passar por cima dá o foco, o clique escolhe (as linhas, as abas, os botões do diálogo, os comandos de baixo, as
setas do esquema), a roda rola, o botão direito ou o Mouse4 voltam. Controle: direcional ou analógico esquerdo, A,
B, LB/RB, Start (e X, Y nas opções). Enquanto uma tela está de pé, `psiInput_SetModal` deixa o jogador 0 sem
ações e o mouse dos menus do jogo parado; quando o menu do jogo fecha (a tela ainda está saindo), o jogo volta a
receber a entrada, e o que ainda está apertado fica de fora até ser solto; a tecla que abriu a tela, e a que abriu
uma confirmação, também só contam depois de soltas. Os três comandos novos da tabela (`menu_tab_prev` Q/LB,
`menu_tab_next` E/RB, `menu_defaults` R/X) não têm ação do jogo e não tiram botões do controle do PS2.

**Movimento** (tempo real, na taxa do monitor; nas capturas, o relógio dos ticks): as telas aparecem em 0,25 s
com as colunas entrando em sequência; o foco, as abas, as listas, os valores, as chaves e a rolagem animam; o fim de
missão é lento de propósito (`docs/design.md`, "Movimento").

### Capturas

- A pausa: `--input "none:1 enter:0.2" --wait 2`; uma aba direto: `--open-menu-page 4000004b --menu-tab
  <pause|objectives|controls|score> --wait 1.5`; as opções: `--open-menu-page 40000060 --menu-tab <categoria>` (com
  `gameplay`); a mira desligada pela aba e o jogo de volta: `--level 07000012 --open-menu-page 40000060 --menu-tab
  gameplay --input "none:1.2 down:0.1 none:0.3 space:0.1 none:0.4 escape:0.1 none:0.4 escape:0.1 none:0.6 enter:0.1"
  --wait 4.5` (`hud ... mira 0`); as legendas ligadas na pausa da cena de 07000001: `--level 07000001 --no-subtitles
  --open-menu-page 40000060 --menu-tab interface --input "none:1.2 down:0.1 none:0.3 down:0.1 none:0.3 space:0.1
  none:0.4 escape:0.1 none:0.4 escape:0.1 none:0.6 enter:0.1" --wait 15`; a
  confirmação: `--input "none:1 enter:0.2 none:0.6 down:0.1 none:0.3 space:0.1" --wait 2.8`.
- O fim de missão: `--input "none:10 space:0.3" --wait 13.5` (o menu abre em ~10,1 s; `--wait 10.45` e `11` mostram
  o aparecer); REPLAY: `"none:10 space:0.3 none:2.5 space:0.3" --wait 16` (`recomecos 1`, vida cheia).
- O resultado: `--win 1 --shot-level 07000048 --wait 2`; as estatísticas com `--input "none:1 e:0.1"`; CONTINUE com
  `--input "none:1.5 space:0.1" --shot-level 07000014` (a próxima missão).
- `--input` também leva o controle (`pad_start`, `pad_rb`, `pad_a`...) e o mouse na tela virtual (`cmouse=x,y`,
  `+lmb` clica).
- Um recomeço pela pausa vai para a primeira fase da missão (outra fase): o relógio do `--input` recomeça nela e o
  roteiro se repete, como no estilo Original.
- O relatório tem as linhas `moderno ...` (as opções), `pausa_moderna ...` (aba, foco, lista, diálogo, a aba e a
  linha do jogo), `fim_moderno ...` (tempo, foco, motivo), `resultado_moderno ...` (vista, foco) e `ponte_moderna
  ...` (os pedidos executados: abas, linhas, botões, perdidos, a fila). `NF_MENU_DUMP=1` acrescenta os controles da
  página atual de cada gerente (id, tag, tipo, estado, retângulo, textos, as linhas das listas).
- Conferência (em `out/shots/modern_menus`): cada tela e aba em 1920x1080, 3840x2160 e 1024x576, a pausa em
  2560x1080 e com os glifos do controle, e dois quadros do aparecer do fim de missão. As capturas antigas das opções
  estão em `out/shots/modern/final`. No estilo Original, a pausa, o fim de missão e o resultado continuam iguais
  pixel a pixel.

### O menu principal no estilo Modern

Sem `--level`, o caminho de um jogador do menu principal (07000048) também é Modern, ainda decidido pelos tratadores
do jogo (`src/ui/modern_front.h`, `FrontScreen`; a ponte lê as páginas em `MmFront` e manda dois pedidos a mais:
`Focus`, o cursor do gerente num controle, e `Step`, a roda andando um passo de cada vez como no direcional):

| Na tela Modern | No jogo |
|----------------|---------|
| o título (a arte do jogo, "Press the START button", o texto legal) | as páginas de idioma, cartão, filmes, "Press START" e a procura do perfil; escolher é o X no rótulo de tela cheia de 0x40000009 (o script dele mostra 0x4000004a), só depois do texto legal |
| NIGHTFIRE, MULTIPLAYER, CODENAMES | o foco no botão 0x10000002/3/4 e o X (`C_GONIGHTFIRE`, a caixa "not available yet" de `C_GOMULTIPLAYER`, `C_GOCODENAMES`) |
| OPTIONS, QUIT GAME (só do port) | a tela de opções embutida; o diálogo de sair (voltar no menu principal também o abre), que fecha o programa |
| uma linha das rodas (codinomes, dificuldade, missões, menu do codinome) | a roda na posição (`Step`: o 0x49 de cada passo refaz a roda do jogo) e o X no tratador dela |
| Delete / X do controle num codinome do cartão; Y do controle | o círculo (apagar, com a pergunta do cartão); o quadrado (criar) |
| a aba DOSSIER do mapa (E, RB, X do controle) | o quadrado na roda das missões: o dossiê do jogo, como o dossiê Modern (abaixo) |
| CREDITS (só do port) | `Show` da página dos créditos 0x40000030: a mensagem 0x44 ao gerente que o script do "Credits" da AV Options manda (no Modern a AV Options é a categoria AUDIO); o `P_CREDITS_Handler` rola as linhas e volta ao menu principal sozinho |
| uma missão bloqueada | nada (o cadeado e um aviso); a de carro segue o caminho do port (pulada, a seguinte liberada) |
| Controller Setup, Advanced Options, AV Options | as opções Modern em CONTROLLER, GAMEPLAY e AUDIO no lugar das páginas do jogo; o que mudar marca o codinome como mudado (`cn_modified_flag`, como o "escolher" dessas páginas); Driving Controller e Multiplayer Options ficam travadas |
| uma letra, um algarismo, o espaço, Backspace, Enter (teclado do PC) ou uma tecla da grade (controle, mouse) | a tecla do teclado da tela (o foco nela e o X: `C_KEYBOARD_Handler`), então o limite de 8, as letras, a validação e os códigos de Secret Unlocks são os do jogo |
| a página "Controller Setup" que o jogo mostra depois de criar um codinome | as opções Modern em CONTROLLER; sair delas é o voltar do jogo |
| a caixa de mensagem e a do cartão ("Saving data...", sobrescrever?) | o `Dialog` do kit (sem respostas, a faixa de espera), respondido pela lista da caixa |

A arte do "Press START" (a textura 0x03000074, com o Bond e o logotipo) fica na altura da janela, nítida no título e
desfocada e escurecida atrás dos menus (o desfoque cresce em 0,9 s quando o menu entra; numa janela alta ele passa mais
vezes); o logotipo do jogo (0x0300016d) fica em cima do menu e a imagem da missão (a textura do item do mapa, redonda)
na coluna da esquerda do mapa. São texturas do jogo desenhadas pelo Direct3D entre as listas do Direct2D
(`psi_2d_texture_slot`), então as substitutas HD do pacote do PCSX2 valem. O aviso de controle desconectado (a página
0x4000002f que o jogo mostra quando o controle em uso some) é o diálogo de espera Modern com o texto do jogo e o que
fazer no PC; o jogo cuida da volta (o controle de novo ou uma tecla reabrem a pausa), então a tela não prende a entrada.

**O dossiê** (`src/ui/modern_dossier.h`, `DossierView`): uma tela com a barra de cima RECORDS · REWARDS · GADGETS ·
WEAPONS, sobre as páginas do jogo (`engine/menu_dossier.h`). Cada aba é a página do jogo alcançada pela navegação dele,
um pedido por vez, esperando a página mudar (`MmSnapshot::accepts`: o gerente fora de uma transição e passado o
`input_delay` da página, para o botão não se perder):

| Na tela Modern | No jogo |
|----------------|---------|
| o dossiê abre (do mapa, do resultado ou do bônus) | a página da roda 0x4000002b (o íris, a roda no começo): o X na roda, os recordes |
| uma aba (Q/E, LB/RB, o clique) | o voltar do jogo (a seção volta à página da roda), o passo da roda 0x1000010c até a seção e o X (`C_SBDOSSIER_Handler`: os recordes e as recompensas na hora, os aparelhos e as armas depois do íris) |
| uma missão dos recordes ou das recompensas (cima/baixo, esquerda/direita, o mouse) | o passo do rádio 0x10000174/0x1000017a, como o direcional: o 0x49 do rádio refaz a página (`C_RBDSRECORDS/DSREWARDS_Handler`) |
| um item dos aparelhos ou das armas | o passo da roda 0x1000016f/0x10000173 (o texto com a linha da melhoria) |
| voltar (Esc, B, o botão direito) | o voltar do jogo duas vezes (a seção, a página da roda): o jogo volta ao mapa ou ao resultado |

O que aparece é o que os tratadores escreveram: nos recordes, o recorde, os momentos Bond, as melhorias e a melhor
medalha com a imagem dela (o 007 de bronze, prata, ouro ou platina do jogo); nas recompensas, as quatro da missão (a
imagem e o texto do jogo, ou o cadeado e a medalha que falta); nos aparelhos e nas armas, a imagem, o nome e o texto
(com a linha da melhoria em ouro) e o nível de melhoria do grupo de cada item (`Menu_GetObjectUpgradeLevel`). A lista
do meio tem as 12 missões com a melhor medalha ou as recompensas ganhas (quatro losangos), ou os itens com "Upgrade"; à
esquerda, a imagem redonda da missão ou do item. As imagens são texturas do jogo pelo Direct3D (as substitutas HD
valem). Enquanto o jogo troca de página, o conteúdo da aba velha some e o da nova entra.

**Os créditos** (a vista Credits do `FrontScreen`): a linha CREDITS do menu principal Modern mostra a página dos créditos
do jogo (no estilo Original eles continuam na AV Options, que no Modern é a categoria AUDIO das opções). O
`P_CREDITS_Handler` põe uma linha nova a cada 14 ticks nos 26 pares de rótulos 0x10000213/0x10000214 e sobe todas 2
pixels por tick; a tela lê os rótulos visíveis (o texto, a posição e a fonte do formato do jogo) e os desenha no tipo do
kit, centrados na coluna do jogo: as empresas (fonte 1) em versalete grande e claro com um traço de ouro, os cargos
(fonte 3) em versalete pequeno espaçado, os nomes e o texto legal (fonte 2) no tipo das linhas; as linhas aparecem
embaixo e somem em cima. O ritmo e o fim são os do jogo: 18 linhas depois da última a página escurece (o alfa do rótulo
0x10000215, desenhado por cima) e volta ao menu principal, que fica com o foco em CREDITS; voltar sai antes.

Capturas (sem `--level`): `--input "none:4.5 enter:0.1" --wait 7.5` o menu principal; `"... none:2 enter:0.1" --wait
9.5` a roda de codinomes; `"none:4.5 enter:0.1 none:2 enter:0.1 none:2 up:0.1 none:0.6 enter:0.1 none:1.5 J:0.1 none:0.1
A:0.1 none:0.1 M:0.1 none:0.1 E:0.1 none:0.1 S:0.1 none:0.5" --wait 12.5` JAMES digitado; com `enter:0.1` a mais, o
cartão grava e o jogo mostra Controller Setup como as opções Modern (`--wait 14`); o aviso de controle desconectado:
`--level 07000012 --input "none:1 pad_a:0.2 pad_off:5" --wait 3.5` (`pad_off` desconecta o controle simulado); o dossiê:
`--unlock-all --input "none:4.5 enter:0.1 none:2 enter:0.1 none:2 enter:0.1 none:1.5 enter:0.1 none:1.5 down:0.1
none:0.6 enter:0.1 none:1.5 E:0.1 none:2" --wait 18` (o mapa, E: RECORDS; mais `E:0.1 none:2` a cada aba, com `--wait`
2 s a mais); os créditos: `--input "none:4.5 enter:0.1 none:2.5 down:0.1 none:0.3 down:0.1 none:0.3 down:0.1 none:0.3
down:0.1 none:0.5 enter:0.1 none:30" --wait 25`; a aba SCORE com números: `--level 07000012 --at
22.49,1.09,19.155,-1.68 --input "none:4.5 pad_ry=-0.6:0.25 none:0.25 lmb:0.1 none:0.25 lmb:0.1 none:0.25 lmb:0.1 none:1
enter:0.1 none:0.8 E:0.1 none:0.3 E:0.1 none:0.3 E:0.1 none:1.5" --wait 9.5`. O relatório tem as linhas
`frontend_moderno ...` (vista, página, foco, linhas, a posição da roda, diálogo, opções, desfoque, o nome digitado; no
dossiê `| dossie_moderno aba alvo secao foco linhas enviado conteudo`, nos créditos `| creditos linhas escurecer`) e
`aviso_moderno ...`; `ponte_moderna` conta também `focos`, `passos` e `paginas` (os `Show`). Conferência (em
`out/`, fora do git): cada vista em 1920x1080, 3840x2160 e 1024x576, o título, o menu, o mapa e o teclado em 2560x1080,
o menu com os glifos do controle e com o mouse; os fluxos inteiros (teclado, controle simulado, `--save-dir` e
`--win`) carregam a missão; com `--menu-style original` todas as páginas do menu principal ficam iguais pixel a pixel.

## Controles (teclado, mouse e controle)

O jogador 0 recebe o teclado e o mouse por uma tabela de teclas e o controle do PC (XInput) pelo próprio
controle do PS2 do jogo. As duas fontes se juntam no fim do `psiInput_MapInputs` portado
(`src/platform/psi_input.cpp`):

- **Controle.** `nf::PadSource` (`src/platform/gamepad.h`) lê o XInput 1.4 (até 4 controles; um índice
  vazio só é consultado de novo a cada 60 ticks) ou, nos testes e nas capturas, um `ScriptedPadSource`.
  O estado vira o "slot" da libpad (`src/platform/psi_pad.h`, `PsiPadSlot`, os campos do `TSLOTDATA`): A =
  X, B = círculo, X = quadrado, Y = triângulo, Back = Select, Start, LS/RS = L3/R3, direcional; com
  `pad_shoulders = triggers` (padrão) RT/LT fazem R1/L1 (atirar e mirar no estilo NightFire) e RB/LB R2/L2,
  com `bumpers` o contrário. O primeiro controle conectado é o slot 0 (jogador 0), o segundo o slot 4. Daí
  em diante é o código do jogo: `psiInput_PollDevices` (zona morta de 40/127 em cada eixo, configurável),
  `psiInput_MapInputs` com os oito estilos de botões (`PlayerSetting::style`, a aba CONTROLS da pausa) e
  as curvas de `Input_Update` (`StickCompensation2/3`, `MapAnalogStick`). A vibração é a do jogo
  (`Input_RumbleStart` nos tiros e nos ferimentos, com a opção de vibração ligada): o motor pequeno liga e o
  grande recebe a força x 2,5, por uma contagem de ticks; o jogo pausado desliga.
- **Teclado e mouse.** 38 comandos (`src/platform/input_bindings.h`, `kBindCommands`; aba anterior/próxima e
  restaurar padrões só dos menus Modern; o último, a roda de armas, lido segurado pelo `main.cpp`), cada um com até duas
  entradas do teclado, do mouse ou da roda e uma do controle, ligados a uma ou duas das 40 ações (andar
  para a frente = ação 2 positiva; virar à esquerda = ações 0 e 3 negativas; pausa = 30 e 25...) ou a uma ação só
  do port (`PortAction`: recarregar, os cinco espaços dos aparelhos e a roda de armas, que o PS2 não tem;
  `psiInput_PortPressed`).
  Os padrões seguem o costume dos jogos de PC (a tabela abaixo). Uma entrada do controle
  posta na tabela sai do controle do PS2 e passa a valer só pela tabela (religar A para atirar tira o
  agachar do estilo; religar X para recarregar tira o usar do X).
- **Juntos.** Os valores analógicos (andar, andar de lado, virar, mirar, olhar: ações 0-5) são os do
  último aparelho usado (uma tecla ou botão novo, o mouse andando, a roda contra um botão novo do
  controle, um gatilho ou um analógico mexendo); os botões somam. O mouse gira a visão direto
  (`psiInput_TakeMouseLook`); mirando, a sensibilidade vezes `mouse_aim_sensitivity` e dividida pelo zoom.
  Se o controle em uso some, o jogador fica "desconectado" e o `MenuManager_Monitor` do jogo mostra o aviso
  de controle desconectado (página 0x4000002f); o controle de volta, ou uma tecla, volta à pausa. Sem o foco
  da janela, os controles ficam parados.

As teclas padrão (decididas com o usuário em 10/10/2026, no costume do PC; um `nightfire.ini` com `[bindings]`
escritos continua com os dele, ver abaixo):

| Comando (`[bindings]`) | Padrão | Ação do jogo | Antes |
|------------------------|--------|--------------|-------|
| `forward`, `back`, `strafe_left`, `strafe_right` | W, S, A, D | andar (2), de lado (1) | igual |
| `turn_left`, `turn_right`, `look_up`, `look_down` | setas | virar/mirar (0, 3), olhar (4, 5) | igual |
| `jump` | Espaço | 7 | igual |
| `crouch` | C, Ctrl | 8 | igual |
| `fire` | botão esquerdo | 9 (R1) | igual |
| `aim` | botão direito | 19 (L1) | igual |
| `use` | E | 14: usar, ou recarregar se nada foi usado (como o □ do PS2) | igual |
| `reload` (novo) | R | só do port: recarrega sem usar nada | — |
| `fire_mode` (modo de tiro / acessório) | botão do meio, X | 12 (○) | R |
| `vision` | N | 13 (select: visão noturna, raio X) | Tab |
| `weapon_next` | roda para baixo, Z | 16 | Z |
| `weapon_prev` | roda para cima, Q | 15 | Q |
| `gadget_next` (e zoom +) | G | 11 e 6 | 1 |
| `gadget_prev` (e zoom −) | — | 10 e 6 | 2 |
| `gadget_1` ... `gadget_5` (novos) | 1 a 5 | só do port: o n-ésimo aparelho | — |
| `pause` | Enter, Esc | 30 e 25 (Start) | Enter |
| `menu_up`, `menu_down`, `menu_left`, `menu_right` | setas, WASD | 26-29 | igual |
| `menu_select` | Espaço | 31 e 25 (✕) | igual |
| `menu_circle` | Tab | 33 (○) | igual |
| `menu_back` | Esc, Backspace | 34 (△) | Backspace |
| `menu_square` | E | 32 (□) | igual |
| `menu_tab_prev`, `menu_tab_next`, `menu_defaults` | Q/LB, E/RB, R/X | (só dos menus Modern) | igual |
| `weapon_wheel` (novo) | Tab, L3 | só do port: a roda de armas, segurada ("Mira moderna e roda de armas") | — |

O Tab é a roda de armas no jogo (segurada; no controle L3, o único botão que nenhum estilo usa); no menu continua o
círculo (os grupos são separados).
Nenhuma tecla padrão é tecla de depuração do `main.cpp` (V, H, P, T, F2-F4, F9, F12, Page Up/Down: `test_input`
confere). Regras só do PC, com o comportamento do jogo em volta:

- **Esc**: é a pausa (Start) no jogo e o voltar (△) nos menus. Com um menu do jogo aberto, uma entrada do `pause` que
  também é de um comando do menu (`nf::pause_menu_shared`: o Esc, no padrão) vale só no menu (Start e △ juntos se
  anulam em `Menu_InputAction`); fechado o menu, ela fica fora do `pause` até ser solta (não reabre a pausa). No topo
  da pausa (o △ com a pilha vazia: `Manager_SendMessage` 0x5f manda 99 à página), voltar fecha a pausa como o Start
  (`pause_page` em `menu_pc_options.cpp`, contado em `MenuPcStats::back_closes`); no PS2 nada acontece. Voltar da
  pergunta de RESTART/QUIT só fecha a pergunta. A pausa Modern já fazia o mesmo. O Esc também pula cenas e a cortina
  de sangue (a ação 25), como o Enter e o Espaço.
- **R recarrega**: `Player_WeaponFiring` roda a recarga do bloco do usar (os estados 5-7 da arma na mão, as animações
  de recarga) sem `Player_Activate`: uma porta na frente não abre. Também mirando com luneta, onde o usar não recarrega
  (a recarga tira a mira pelos estados 6-8, como a recarga automática do pente vazio). O E continua "usar ou
  recarregar".
- **A roda**: para baixo a próxima arma, para cima a anterior (um dente = um tick apertado). Mirando com uma arma de
  zoom (o zoom máximo dela acima de 1, o caso em que o direcional dá zoom), `Player_Zoom` pede a roda
  (`psiInput_SetWheelZoom`): no tick seguinte os dentes não vão para a tabela e cada um aproxima (para cima) ou afasta
  o alvo do zoom como 9 ticks do direcional (x 1,025^9 ≈ 1,25), até 1 e o máximo da arma; o zoom anda até o alvo pelo
  caminho do jogo (15% por tick, com o desfoque) e a classe guarda o alvo. 0700000c com o rifle: 3 dentes levam o zoom
  de 3,16 a 6,16.
- **1 a 5**: `Player_PortSelectGadget` escolhe o n-ésimo aparelho que o jogador tem, na ordem do ciclo do jogo
  (`Player_WeaponChange`, grupo 1: de 0x4a a 0x5e, as classes que entram na troca), com as mesmas recusas (travessia,
  arma no meio de uma animação, mirando) e a variante e as anotações do ciclo; a troca passa pela máquina de estados da
  arma na mão. Um espaço vazio, o aparelho que já está na mão ou um sem munição não fazem nada; a numeração não muda
  quando um fica sem munição. 07000005: 1 o taser (74), 2 o laser (78), 3 o gancho (80), 4 a micro-câmera (84), 5 nada.
  G cicla (o `gadget_next` do jogo; com mais de cinco aparelhos, o sexto só por ele).
- **.ini antigo**: um comando escrito em `[bindings]` vale como está; um comando ausente (no padrão) perde a entrada que
  um comando escrito do mesmo grupo usa (`fire_mode = r` deixa o `reload` sem R, `gadget_next = 1` deixa o `gadget_1`
  sem 1), a não ser que um obrigatório ficasse sem tecla (então fica o padrão, com o conflito). Gravar de novo escreve o
  comando que perdeu a entrada.

As abas (pausa > OPTIONS > CONTROLS e KEYS, `src/engine/menu_pc_controls.h`), com os controles das páginas
de opções do jogo:

- **CONTROLS**: o aparelho em uso, a sensibilidade do mouse (a barra logarítmica abaixo, "Mouse speed 2.20"), o
  mouse mirando, a sensibilidade e a zona
  morta do controle (barras de volume, o valor no nome), inverter o Y do mouse, inverter o Y do controle (a
  opção do próprio jogo, `PlayerSetting::invert_look`, a mesma da aba CONTROLS da pausa), vibração
  (`PlayerSetting` +0x09) e os gatilhos como R1/L1 (rádios Off/On). Nove linhas: as duas últimas entram
  rolando. Cima/baixo muda de linha, esquerda/direita mexe na barra ou troca o rádio, Espaço troca o rádio.
- **KEYS**: a lista com moldura e setas com cada comando, o teclado/mouse e o controle (entre parênteses, o
  que o estilo de botões faz quando a tabela não religa nada, por exemplo `(RT)` para atirar). Escolher (ou
  clicar) uma linha espera a tecla, o botão do mouse, um dente da roda ou o botão do controle novo; Esc cancela,
  Delete limpa o teclado do comando. Conflitos: uma entrada fica num só comando de cada grupo (jogo ou
  menu; Espaço pode pular e escolher no menu); ligada a outro comando, sai do anterior; se o anterior é
  obrigatório (pausa, escolher, voltar, cima e baixo no menu) e ficaria sem tecla, ele fica com a tecla
  que este tinha, e sem ela a troca é recusada. A última linha restaura os padrões. A linha de baixo diz o
  que aconteceu ("F -> Fire", "Enter -> Use / reload (Pause (Start) now uses E)").

Cada mudança vai para o `nightfire.ini` na hora (nas capturas, só com `--config`). Só vão para o arquivo as chaves
que ele já tem e as que não estão no padrão (o mesmo para cada comando de `[bindings]`): mudar uma opção não escreve
as outras com o valor padrão. `vibration`, `pad_style` e `pad_invert_y` são opções do jogo e seguem a regra delas (ver
"Opções do codinome"): só vão para o arquivo quando o jogador as muda. Um arquivo com todas, como este:

```ini
[controls]
mouse_sensitivity = 0.0022     ; radianos por contagem (também --sensitivity)
mouse_aim_sensitivity = 1      ; multiplica o mouse mirando (que também divide pelo zoom)
mouse_invert_y = 0
pad_sensitivity = 1            ; multiplica o analógico de olhar antes das curvas do jogo
pad_deadzone = 0.315           ; fração do curso (0,315 = os 40/127 do PS2; a curva do jogo tem a dela)
pad_invert_y = 0               ; PlayerSetting::invert_look
vibration = 1                  ; PlayerSetting +0x09
pad_style = 0                  ; 0-7 ou o nome (NightFire, Moonraker, Octopussy, Goldfinger, Dr. No,
                               ; Thunderball, GoldenEye, Classic Bond)
pad_shoulders = triggers       ; triggers: RT/LT = R1/L1; bumpers: RB/LB = R1/L1

[ui]
; DrawInfo[2] (AV Options)
subtitles = 1

; As opções do jogo (Advanced Options, AV Options): só as que um menu mudou. crosshairs: a mira aparece sem
; mirar; crouch e manual_aim: toggle (alterna) ou hold (segurar); volumes de 0 a 100.
[game]
auto_aim = 1
crosshairs = 1
crouch = toggle
manual_aim = hold
weapon_auto_switch = 1
flashing_objects = 1
hud_always_on = 1
music_volume = 70
effects_volume = 100

[bindings]
forward = w
fire = mouse1, rctrl
jump = space, pad_a
menu_square =
```

Nomes das entradas: letras e dígitos, `f1`-`f24`, `num0`-`num9`, `space`, `enter`, `tab`, `backspace`,
`escape`, `shift`/`lshift`/`rshift`, `ctrl`/`lctrl`/`rctrl`, `alt`/`lalt`/`ralt`, `up`, `down`, `left`,
`right`, `insert`, `delete`, `home`, `end`, `pageup`, `pagedown`, `capslock`, `num*`... as de pontuação
(`semicolon`, `comma`, `minus`, `period`, `slash`, `grave`, `lbracket`, `backslash`, `rbracket`,
`apostrophe`, `oem102`, `abnt_c1`), `mouse1`-`mouse5`, `wheel_up`, `wheel_down` e do controle `pad_a`,
`pad_b`, `pad_x`, `pad_y`, `pad_lb`, `pad_rb`, `pad_lt`, `pad_rt`, `pad_back`, `pad_start`, `pad_ls`,
`pad_rs`, `pad_up`, `pad_down`, `pad_left`, `pad_right`, `pad_lx+`, `pad_lx-`, `pad_ly+` (para cima),
`pad_ly-`, `pad_rx+`, `pad_rx-`, `pad_ry+`, `pad_ry-`. Vazio: nenhuma; um comando ausente fica no padrão.
Comandos: `forward`, `back`, `strafe_left`, `strafe_right`, `turn_left`, `turn_right`, `look_up`,
`look_down`, `jump`, `crouch`, `fire`, `aim`, `use`, `reload`, `fire_mode`, `vision`, `weapon_next`, `weapon_prev`,
`gadget_next` (e zoom +), `gadget_prev` (e zoom -), `gadget_1` ... `gadget_5`, `pause`, `menu_up`, `menu_down`,
`menu_left`, `menu_right`, `menu_select`, `menu_circle`, `menu_back`, `menu_square`, `menu_tab_prev`,
`menu_tab_next`, `menu_defaults`.

A sensibilidade do mouse (`mouse_sensitivity`, radianos por contagem do sensor) nas duas páginas é uma barra
logarítmica (`nf::mouse_sens_from_pos`/`mouse_sens_to_pos`): 61 posições, 8 por dobro (9% por passo), com o padrão
0,0022 na posição 44; vai de 0,0000486 a 0,0088 (o valor mostrado é x 1000: 0,05 a 8,80). Uma volta completa leva de
410 a 2,3 cm a 800 DPI: de 400 a 3200 DPI cabem as distâncias de 5 a 100 cm. O padrão é 9,1 cm por volta a 800 DPI
(4,5 cm a 1600). A tela Modern mostra a volta em centímetros a 800 e a 1600 DPI no medidor da descrição; a página
Original, só o valor (os centímetros não cabem na coluna do nome). Um valor do `.ini` fora dos passos continua exato
até a barra ser mexida.

Os oito estilos de botões (o controle do PS2; ✕ = A, ○ = B, □ = X, △ = Y; R1/L1 = RT/LT com os gatilhos):

| Estilo | Analógicos | Atirar | Mirar | Pular | Agachar | Usar | Modo de tiro | Visão | Armas | Aparelhos/zoom |
|--------|------------|--------|-------|-------|---------|------|--------------|-------|-------|----------------|
| 0 NightFire | esquerdo anda, direito olha | R1 | L1 | △ | ✕ | □, R2 | ○ | Select | ←/→ (e L2) | ↑/↓ |
| 1 Moonraker | esquerdo anda, direito olha | R1 | L1 | △ | L2 | ✕ | □ | ○ | ←/→ (e R2) | ↑/↓ |
| 2 Octopussy | esquerdo anda e vira, direito anda de lado e olha (L1: o esquerdo anda de lado) | R1 | L1 | △ | L2 | ✕ | □ | ○ | ←/→ (e R2) | ↑/↓ |
| 3 Goldfinger | esquerdo anda, direito olha | R1 | L1 | △ | ✕ | L2 | R2 | □, ○ | ←/→ | ↑/↓ |
| 4 Dr. No | direito anda, esquerdo olha | R1 | L1 | ↑ | ↓ | L2 | R2 | ←, → | □/○ | △/✕ |
| 5 Thunderball | esquerdo anda e vira, direito anda de lado e olha | R1 | L1 | △ (sem L1) | ✕ (sem L1) | L2 | R2 | □, ○ | ←/→ | ↑/↓ e △/✕ |
| 6 GoldenEye | esquerdo anda e vira, direito anda de lado e olha; L1/R1 de lado | ✕ | R2 | △ | L2 | □ | ○ | Select | ↑/↓ | ←/→ |
| 7 Classic Bond | esquerdo anda e vira, direito anda de lado e olha | R1 | L1 | △ | L2 | ✕ | □ | Select | ↑/↓ (e R2) | ←/→ (e ○) |

Em todos: Start pausa; nos menus ✕ escolhe, △ volta, o direcional e o analógico esquerdo andam. A aba
CONTROLS da pausa mostra o mapa de cada estilo com os textos do jogo (`Menu_DisplayControllerStyleList`).

Capturas: `--input` aceita um controle simulado (`pad_a`, `pad_rt`, `pad_lx=1.0`...; conectado só quando
o roteiro usa `pad_*`) e o mouse dos menus (ver "Mouse nos menus"), e `--open-menu-page 40000060 --menu-tab
<aba>` abre a pausa e a página de opções nessa aba meio segundo depois da carga. Conferência (07000012,
1024x576): pausa > OPTIONS > CONTROLS e a aba; KEYS com atirar religado para F pela própria aba, e depois F atirando no jogo (pente 7 -> 6); o mesmo
F pelo `fire = f` de um `--config`; o controle simulado andando ~8,6 m, virando e atirando; a página com o
controle em uso ("Controller 1", o direcional ajustando uma barra); a aba CONTROLS com o estilo
Goldfinger. Ainda sem um controle de verdade: só o simulado foi exercitado.

Conferência das teclas de PC (`--fixed-video-hz 60`, 1024x576):

```bash
# Esc abre a pausa (Modern); um segundo Esc fecha (nos dois estilos: menus status 0)
nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:1 escape:0.2" --wait 2 --screenshot out/shots/esc.png
# R recarrega: arma 2 estado 6 pente 4 municao 41 (com --wait 6: pente 7 municao 38)
nightfire.exe --level 07000005 --fixed-video-hz 60 --input "none:1.5 lmb:0.1 none:0.5 lmb:0.1 none:0.5 lmb:0.1 none:0.7 R:0.1 none:3" --wait 3.75 --screenshot out/shots/recarga.png
# a roda: sem mirar, a próxima arma (wheel=-1: da pistola ao soco); mirando com o rifle, o zoom (3,16 -> 6,16)
nightfire.exe --level 0700000c --fixed-video-hz 60 --input "none:1.5 rmb:1 rmb+wheel=3:1.5" --wait 3.8 --screenshot out/shots/zoom.png
# 1 a 5: o n-ésimo aparelho (07000005: 3 = o gancho, arma 80)
nightfire.exe --level 07000005 --fixed-video-hz 60 --input "none:1.5 3:0.1 none:2.5" --wait 4 --screenshot out/shots/aparelho.png
# a categoria CONTROLS da tela Modern com o foco na sensibilidade do mouse (a volta em cm no medidor)
nightfire.exe --level 07000012 --open-menu-page 40000060 --menu-tab controls --input "none:1 down:0.1 none:0.4" --wait 2 --screenshot out/shots/mouse.png
```

## Mouse nos menus

Só do port: todos os menus (a pausa, "Mission Failed", a caixa SIM/NÃO, o resultado e as outras páginas do
menu principal, a página de opções do PC) aceitam o mouse. O `ACTION.ELF` não tem código de mouse: o
`CursorInfo` do jogo é o "cursor" do gerente (`M_MANAGER::cursor`), um sprite escondido de um texel de
0x03000042 que só marca o foco; o foco dos menus segue esse cursor (`Page_Update` -> `Menu_GetControl`) e o
direcional o leva de controle em controle (`Menu_FindControl`). O mouse entra pelo mesmo caminho
(`src/engine/menu_mouse.cpp`, chamado por `MenuManager_Update` no começo do tick do gerente que recebe a
entrada; é a única mudança no núcleo dos menus):

- **Passar por cima** põe o cursor do gerente sob o mouse, e o `Page_Update` do jogo muda o foco (com os
  eventos 0x4f/0x4e e o som). Controles com o mesmo id do foco são um grupo só, porque o jogo compara o foco
  por id (as abas e as listas da pausa são todas 0x10000028): passar por cima deles não tira o foco. O nome
  de uma linha (um rótulo desabilitado com o id e a tag do controle da linha) conta como o controle. Numa
  lista, a linha sob o mouse fica escolhida, como com cima/baixo (na aba CONTROLS da pausa isso troca o
  estilo de botões, como o direcional).
- **Clique esquerdo**: botões, caixas e rótulos recebem a ação de escolher (0x1f, por um tick, lida pelo
  código dos controles como a do Espaço); clicar numa aba da pausa só leva o foco a ela (a troca de aba é o
  evento de foco do jogo; os outros botões de mesmo id, como as teclas do teclado da tela do menu principal, são
  botões comuns: passar por cima leva o foco e clicar escolhe); numa roda do menu principal (a barra minúscula com o
  foco e os cinco rótulos dos itens), clicar no rótulo do meio escolhe e nos outros gira a roda até ele, e a roda do
  mouse a gira em qualquer lugar da página; numa lista, a linha sob o mouse é escolhida e escolhida de vez (CONTINUE fecha a
  pausa, REPLAY recomeça, uma linha de KEYS espera a tecla); na barra da lista, as setas andam uma linha e o
  meio pula; num rádio, a seta da esquerda volta e o resto avança; numa barra, as setas andam um passo e o
  trilho põe o valor sob o mouse (o fim do preenchimento) e segue o arrasto. Os passos usam o caminho de
  `Scroll_Update` (posição, som, evento 0x49 ao dono).
- **Roda**: listas uma linha por dente (para cima, a anterior), rádios e barras um passo (para cima, mais);
  a que nenhum controle usou fica para a página (a de opções rola as linhas).
- **Botão direito ou Mouse4**: voltar (a ação do triângulo, 0x22).
- **O cursor** é o do Windows: com um menu aberto o mouse fica solto (sem a captura; o clique não captura) e
  a seta aparece só enquanto o mouse é usado (mexeu ou clicou depois da última tecla ou botão do controle);
  uma tecla ou o controle a escondem e devolvem o foco ao teclado. Quando o menu fecha, a captura volta se
  estava ligada quando ele abriu. Nada do mouse passa para o jogo: com um menu aberto os botões e a roda não
  chegam à tabela de teclas, e os botões ainda segurados quando ele fecha ficam de fora até serem soltos
  (o clique em CONTINUE não vira um tiro).

Capturas: `--input` aceita `mouse=x,y` (o cursor nesse ponto da tela do PS2, 512 x 448, as coordenadas de
`texto2d` no relatório, a partir do trecho), `wheel=n` (n dentes no começo do trecho) e os botões `lmb`,
`rmb`, `mouse4`. O relatório tem a linha `mouse dentro x y ativo alvo id/tag tipo | focos cliques escolhas
passos valores linhas voltar`. Exemplos (07000012, 1024x576):

```
--input "none:1 enter:0.2 none:0.5 mouse=256,228:0.6" --wait 2.5                       # passa por RESTART
--input "none:1 enter:0.2 none:0.5 mouse=212,119:0.3 mouse=212,119+lmb:0.1 none:0.5" --wait 3   # clica OBJECTIVES
--input "none:10 space:0.3 none:2 mouse=256,204:0.3 mouse=256,204+lmb:0.1 none:4" --wait 17     # REPLAY: recomecos 1
--open-menu-page 40000060 --menu-tab video --input "none:1 mouse=166,119:0.3 mouse=166,119+lmb:0.1 none:0.4" --wait 2   # aba GRAPHICS
--open-menu-page 40000060 --menu-tab controls --input "none:1 mouse=300,167:0.2 mouse=300,167+lmb:0.2 mouse=395,167+lmb:0.6" --wait 2   # arrasta a barra
--open-menu-page 40000060 --menu-tab keys --input "none:1 mouse=150,220:0.3 mouse=150,220+lmb:0.1 none:0.3 f:0.1 none:0.6" --wait 2.5   # "F -> Turn left"
```

## Iluminação e névoa

O PS2 ilumina cada vértice no microprograma da VU1 e põe a névoa numa passada sobre o quadro
(`docs/formats.md`, "Microprograma da VU1", com as fórmulas). O renderizador faz as mesmas contas; a
referência na CPU, usada pelos testes, é `src/platform/vu1.cpp`.

- Entradas como a VU1 as vê: `nf::decode_gfx` guarda a cor do disco na escala do GS (0x80 = 1.0; r, g
  e b vão até 0xff, o dobro da textura: metade dos vértices dos dados passa de 0x80), a normal das
  malhas rígidas como o inteiro do UNPACK / 128 e a das malhas com skinning como a do disco / 64, e
  marca as partes cujos lotes têm w = 0x10 na GIF tag (`SubMesh::env`, rotina do mapa de ambiente).
- Por desenho (`PsiDrawCall`, copiado por `collect_frame` para o `DrawItem`): o tweak (`TAmbient`), as
  até duas luzes de `psiLight_SetLights` (cor `TColor0/1` e posição, levada ao espaço do objeto como
  `FillMatrixChainRot`: `(L - t) * R^T`; no mundo com skinning) e as linhas 0 e 1 da câmera.
- Vertex shader: sem luzes, a cor do vértice vezes o tweak (rotinas 0x25e/0x2a3); com luzes (0x340),
  `min(255, tweak * cor + TColor1 * s1 + TColor0 * s0)` com `s = max(0, max(0, 1/d² - 1/r²) * (n . D))`;
  nas partes `env` (0x2e0), sem luzes e com `ST = 0.5 + (n . linha da câmera) / 256`; `floor` no fim,
  como o `FTOI0`. Com skinning, posição pelos dois ossos e a normal da VU1 girada só pelo primeiro osso e
  truncada, como o `SkinIt` da VU0. O que o jogo desenha sem luzes (salas sem luz que as alcance, o céu,
  com `psiLight_SetLights(nullptr, 0, ...)`) recebe só a cor do vértice e o tweak.
- Pixel shader: o `MODULATE` do GS, `Ct * Cv / 128` (saturado) e `At * Av / 128`; o teste de alfa usa
  esse alfa inteiro, como o GS.
- Névoa: `psiPostSolid` registra onde o jogo chamaria `psiDrawFogPass` (`PsiFrame::fog_at`, com a cor e o
  Ps2Far1 da fase); `collect_frame` põe ali um item `fog_pass`, e o renderizador desenha um triângulo da
  tela inteira que lê a profundidade (o buffer de profundidade é `R32_TYPELESS`, lido como
  `R32_FLOAT`), recupera a distância de cada pixel pelos planos da projeção (`FrameParams::z_near`,
  `z_far`), calcula o Z do GS (`A / z + B` pelo Ps2Far1, inteiro, módulo 2^24; 1 onde nada escreveu Z) e
  mistura `névoa + (cor - névoa) * ((Z >> 8) & 0xff) / 128` onde Z < 0x7fff. O céu da camada 0 e a lista
  transparente vêm depois e não levam névoa. Os objetos transparentes perdem alfa com a distância como
  no PS2: `psiDrawObjectMatrix` aplica o trecho de `FillMatrixChainRot` com `AlphaFogObjects`
  (`psiSetTweakA`), inclusive o acúmulo em `olda` entre desenhos sem tweak novo.
- O plano de longe do PC continua em 20 km (o do PS2 é 2^24 m, praticamente infinito); o Ps2Far1 da
  fase só muda a escala do Z do GS, isto é, a névoa.

Divergências: o recorte da caixa 0 que decide se o alfa dos transparentes cai (`CheckObjectClipBox`) usa
o volume da câmera no espaço dela (lados pelo campo de visão do `viewer_tag`, o volume dobrado para a
bandeira de recorte) em vez das matrizes de recorte da VU0, e vale para o objeto inteiro; a normal
com skinning não leva a fração que os índices de osso somam no `ITOF15` do `SkinIt` (menos de 0,003
em 128); a interpolação das cores é a da GPU (perspectiva correta), não a linear na tela do GS; o Z da névoa é calculado em float por pixel.

Conferência: com `--debug-shading --no-fog`, as capturas do ponto de partida de 12 fases (as 7 com
névoa, 07000001, 07000003, 0700000a, 07000014 e 0700001b) ficam iguais às do commit anterior, a não ser
pelas bordas de folhagens com teste de alfa (o alfa agora é `At * Av / 128` exato; 255 pixels em
07000001, 19 em 0700000d) e pelos transparentes que perdem alfa com a distância nas fases com névoa
(5516 pixels em 07000004, 985 em 07000005). Com a iluminação do jogo a imagem muda inteira: as cores
dos vértices (a luz pré-calculada das fases) não entravam antes por padrão. As luzes somam em objetos
perto de luminárias (portas de 07000001, o corpo do Bond); o mapa de ambiente aparece, por exemplo, no
painel do satélite de 0700001b; a névoa de 0700000c e 0700000d clareia ou escurece prédios e montanhas
ao longe.

## Menu principal

Sem `--level`, o `nightfire.exe` abre o menu principal do jogo (07000048) e o jogo segue pelos tratadores dele
(`docs/engine.md`, "Menu principal"), no estilo Modern (o padrão: ver "O menu principal no estilo Modern") ou, com
`--menu-style original`, nas páginas do jogo, também com a camada HD (F4): "Press START" (Enter, depois dos 4 s do texto
legal), o menu principal, a roda de codinomes da campanha ("New Codename", "Play without saving" e os do cartão), a
dificuldade, o mapa das missões (só as liberadas; Enter começa a missão com o filme de abertura: ver "Filmes"),
os codinomes, o teclado da tela e o menu do codinome. Teclado: setas andam, Espaço ou Enter escolhem, Backspace volta;
o mouse também: passar por cima e clicar nos botões e nas teclas do teclado da tela, clicar nos rótulos de uma roda
(o do meio escolhe, os outros giram até ele) e a roda do mouse para girar a roda em qualquer lugar da página.

Os filmes (as aberturas, o clipe, a demonstração, o fim de jogo e os fundos animados das páginas) tocam: ver "Filmes".
O multijogador mostra "Multiplayer is not available yet."; uma missão de carro (o DRIVING.ELF não foi
portado) é pulada: conta como feita, sem medalha, a seguinte fica liberada e o mapa mostra um aviso. As páginas de
opções do codinome: ver "Opções do codinome".

### Dossiê

O quadrado (o X de um controle Xbox, ou a tecla do `menu_square` nos atalhos: E, a mesma da aba DOSSIER do mapa Modern)
no mapa das missões, no resultado e no bônus abre o dossiê do jogo (`docs/engine.md`, "O dossiê"), no estilo Original
(com a camada HD, a barra de ajuda mostra a tecla E no lugar do quadrado) e no Modern (o dossiê Modern, acima): a roda Records / Rewards / Gadgets / Weapons; os recordes (o recorde, os momentos Bond, as melhorias e a
melhor medalha de cada missão, escolhida com as setas), as recompensas (as quatro de cada missão, vazias até a medalha),
os aparelhos e as armas (a imagem, o nome, a descrição e a linha da melhoria ganha; a pistola troca de modelo com as
melhorias). As imagens são as do jogo, nada 3D. Backspace volta. No estilo Modern, a linha e a aba DOSSIER do resultado
e a aba DOSSIER do mapa mandam o quadrado: as páginas do dossiê aparecem como o dossiê Modern e voltar dele reabre o
resultado ou o mapa.

`--unlock-all` (depuração) põe nos dois perfis padrão o que um jogo terminado com platina em todas as missões teria
(as missões liberadas, os bits das medalhas, as melhorias), para as capturas mostrarem as recompensas: "Play without
saving" e os codinomes novos o usam; um codinome do cartão continua com o que gravou. As melhorias valem também com
`--level` (07000005 começa com a pistola melhorada, a arma 8, no lugar da 2). O relatório tem a linha `dossie
pagina X ...` (a roda, a missão dos recordes, o recorde, as melhorias, a medalha, as recompensas visíveis).

```bat
build\Release\nightfire.exe --unlock-all --screenshot out\shots\rewards.png --wait 19 --input "none:4.5 enter:0.1 none:2 enter:0.1 none:2 enter:0.1 none:1.5 enter:0.1 none:1.5 down:0.1 none:0.6 enter:0.1 none:1.5 pad_x:0.1 none:2 down:0.1 none:0.6 enter:0.1 none:1.5"
```

### Trapaças (a página de depuração)

As trapaças C_CHCH* do jogo são as caixas de marcar da página de depuração 0x40000007 do menu de jogo, que a versão
americana cria com toda pausa mas nunca mostra (`docs/engine.md`, "Trapaças e a página de depuração"). Com
`--debug-menu` (depuração), F9 numa fase abre a pausa nessa página; voltar (Backspace) volta à pausa e Enter fecha.
Mouse e teclado funcionam como nas outras páginas. O disco tem "1" no lugar do nome de cada caixa; o port põe nomes
(em inglês; em português com `[ui] language_pc = pt`). O que cada uma faz no PC:

| Caixa | Efeito |
|-------|--------|
| Debug controls, AI debug, Lockup check, Brightness | só guardam o valor (nada no executável lê) |
| Fly mode | voo livre (`Player_SSScanMode`): o analógico/mouse gira, andar voa no plano do olhar, pular e agachar sobem e descem; a colisão continua |
| All weapons | todas as armas e aparelhos com 999 tiros e colete 50 |
| Invincible | o jogador não perde vida (`CheatInfo` +0, gravado no perfil) |
| HUD | liga ou desliga todos os painéis do HUD |
| Coordinates | as coordenadas no painel da vida |
| Widescreen | `DrawInfo[1]` (16:9); no PC a proporção vem da janela, então só muda o que o perfil grava |
| Characters | desmarcada, os personagens somem e param (`switch_NO_DRONES`) |
| Blind characters | os personagens não veem nem ouvem |
| Dummy player | os personagens miram num jogador falso |
| Allow freeze | o congelamento de `FreezeGame`, que nada liga na versão de venda |
| Music | o volume da música (100 ou 0) |
| Draw all rooms | `switch_ForceDrawAll` (a tecla P) |
| Zero gravity | gravidade zero (`Player_ZeroG`): flutua, usar endireita |
| Unlock all | tudo liberado nos menus (`menu_unlock_everything`: o mapa das missões, as dificuldades) |

"Previews" (as cenas, 0x4000000c) e "Total Score:" (os ajustes 0x40000044): ver "Opções do codinome"; "End
mission" (o botão sem nome do disco) abre 0x4000004c, cuja lista vem vazia do disco: o port põe as seis escolhas do
tratador (fracassar, cumprir, cumprir com bronze, prata, ouro, platina; a medalha escolhida nunca é lida, a pontuação é
a da partida). Nas capturas: `--open-menu-page 40000007` e cliques de mouse (`mouse=x,y+lmb`); a linha `trapacas ...`
do relatório tem o valor de cada trapaça.

```bat
build\Release\nightfire.exe --level 07000012 --open-menu-page 40000007 --input "none:1 mouse=320,121:0.3 mouse=320,121+lmb:0.1 none:0.5 enter:0.1 none:2" --wait 3.5 --screenshot out\shots\nohud.png
```

### Opções do codinome

No menu do codinome (depois de criar ou ler um codinome, ou em "Codenames"), a roda abre as páginas de opções do
jogo, no estilo Original e com a camada HD; teclado, controle e mouse funcionam como nas outras páginas (o mouse nas
rodas de valores: a seta da esquerda volta, o resto avança; a roda do mouse troca o valor; as barras seguem o clique e
o arrasto). Escolher (Espaço, Enter ou X) confirma a página inteira; Backspace volta sem gravar.

| Página | O que faz no PC |
|--------|-----------------|
| Controller Setup | os oito estilos do controle do PS2 (`PlayerSetting::style`) com o desenho do controle e o que cada botão faz no estilo; Tab (o círculo) inverte o eixo Y do controle. Vale para o controle; o teclado e o mouse seguem os atalhos (`[bindings]`, `mouse_invert_y`) |
| Advanced Options | vibração, mira automática, a mira sempre visível, agachar e mirar alternando ou segurando, troca automática de arma, itens piscando e o HUD sempre visível: valem como no PS2 |
| Multiplayer Options | o radar, a vantagem de vida e a mira automática do multijogador: só ficam no perfil (o multijogador não foi portado) |
| AV Options | as legendas valem como no PS2; os volumes da música e dos efeitos (que a barra muda na hora, para ouvir) e o modo do som (mono, estéreo, surround) valem e ficam no perfil e no `nightfire.ini`; a tela dividida e o widescreen só são guardados: o PC desenha na proporção da janela (Hor+); "Restore Defaults", "Credits" e o trailer (que não toca: o vídeo não foi portado) |
| Screen Adjust | a posição da imagem na TV do PS2: só é guardada; nada se move na janela |
| Credits | os créditos do jogo (as 578 linhas, tirados do ACTION.ELF na partida) sobem pela tela e voltam sozinhos; no estilo Modern eles abrem pela linha CREDITS do menu principal (a mesma página do jogo, desenhada no tipo do kit) |

"Secret Unlocks" aceita os códigos do jogo (o teclado da tela em modo de código): liberam missões, personagens e
cenários do multijogador e melhorias de armas, e os de "ou exclusivo" desfazem quando digitados de novo. Depois de um
código certo o jogo mostra a caixa dele.

As opções do jogo valem na próxima missão e vão para o perfil quando o codinome é gravado ("Save Codename"). Com um
`nightfire.ini` para gravar (fora das capturas, ou com `--config`), uma opção mudada num menu também vai para ele na
hora; as legendas, a vibração, o estilo e a inversão do controle, a mira automática, a mira sempre visível, agachar e
mirar, a troca de arma, os itens piscando, o HUD e os volumes (seções `[controls]`, `[ui]` e `[game]`). Na partida o
`.ini` vale por cima dos padrões do jogo; um codinome do cartão lido traz as opções dele (sem mudar o `.ini`); "New
Codename", "Play without saving" e "Default" pegam as do `.ini`. O arquivo só recebe as opções que o jogador mudou (num
menu do jogo, na aba CONTROLS da pausa, no "Controller Setup" ou nas opções do PC): gravar outra coisa (a
sensibilidade do mouse, uma tecla) não escreve o estilo, a inversão e a vibração em vigor. Antes o `pad_style` ia
sempre, e com ele no arquivo "New Codename" e "Play without saving" começavam no estilo 0 (NightFire) em vez do 7
(Classic Bond) do perfil padrão do jogo; um `.ini` que já tem a chave continua valendo. No estilo Modern essas opções
ficam nas opções do PC (GAMEPLAY, INTERFACE > Subtitles, AUDIO, CONTROLLER; ver "Menus no estilo Modern"). O relatório das capturas tem a linha `opcoes_jogo ...`
(o valor de cada opção; `*` quando o `.ini` a lembra) e `olhar` (o ângulo vertical do jogador).

Na página de depuração (`--debug-menu`), "Total Score:" abre os ajustes de dano dos personagens e do jogador e, com o
quadrado, os de tiro da IA (barras; X grava, valem na hora); "Previews" lista as cenas das fases (só as carregadas na
fase aberta tocam, por cima do jogo pausado); a lista das fases tem só "Main Menu".

```bat
build\Release\nightfire.exe --screenshot out\shots\ctl.png --wait 17 --input "none:4.5 enter:0.1 none:2 enter:0.1 none:2 up:0.1 none:0.6 enter:0.1 none:2 enter:0.1 none:5"
build\Release\nightfire.exe --screenshot out\shots\av.png --wait 24 --input "none:4.5 enter:0.1 none:2 enter:0.1 none:2 up:0.1 none:0.6 enter:0.1 none:2 enter:0.1 none:5 backspace:0.1 none:1.5 down:0.1 none:0.6 down:0.1 none:0.6 down:0.1 none:0.6 down:0.1 none:0.6 enter:0.1"
build\Release\nightfire.exe --level 07000012 --open-menu-page 40000007 --screenshot out\shots\tweaks.png --wait 3.5 --input "none:1 mouse=300,401:0.3 mouse=300,401+lmb:0.1"
```

### Cartão de memória

O cartão do PS2 vira uma pasta: `saves\` ao lado do executável, ou a de `--save-dir`. Cada codinome gravado é uma
pasta `BASLUS-20579<NOME>` com os mesmos arquivos que o jogo gravaria no cartão: o ícone (`007.ICN`), o `icon.sys` e
o perfil `BASLUS-20579<NOME>` (os blocos do jogo e o crc no fim, 1026 bytes). A máquina de estados do cartão é a do
jogo (`platform/psi_card.h`), com as mensagens e as perguntas dele, nos textos do PC quando o original fala do cartão
("Saving data. Please do not close the game.", "This codename already exists. Would you like to overwrite it?", apagar;
ver "Textos do PC no lugar dos do PS2"); as chamadas da libmc viram leituras e escritas na pasta, uma por quadro.
Apagar um codinome pelo menu (X na página dos codinomes) apaga a pasta. As capturas (`--screenshot`) sem `--save-dir`
usam um cartão em memória, vazio: nunca tocam nos saves do jogador. `saves/` está no `.gitignore`.

Depois de uma missão cumprida com um codinome do cartão, o CONTINUE do resultado pergunta "Do you want to save your
progress?" (YES já escolhido) e, com YES, o fluxo do cartão grava o perfil com a medalha e as missões liberadas
(perguntando antes se sobrescreve o codinome que já existe); NO segue sem gravar. No estilo Modern as duas caixas
aparecem como o diálogo da tela de resultado (a ponte lê a página 0x4000002a: o memo 0x100000fb e as respostas
0x100000fc).

O relatório das capturas tem as linhas `frontend pagina X perfil N codinome "..." op O ativo A fim D nomes N missoes
bits dificuldade D` e `cartao memoria|pasta pilha N estado S/sub resultado R mensagem M perfis N ops N`. Sem
`--level`, o relógio de `--input` conta da primeira carga e continua na missão escolhida; teclas curtas (0,1 s): uma
tecla ainda apertada quando a missão carrega chega ao jogador (Enter abre a pausa). A última linha de vídeo do
relatório, `roteiro t`, é esse relógio na hora da captura: a duração das cargas varia com a máquina (alguns segundos
entre execuções), então os passos depois da volta ao menu principal precisam de folga. `--win` conta da carga da
missão (a primeira fase fora do menu principal) e `--shot-level 07000048:2` espera a volta ao menu.

```bat
build\Release\nightfire.exe --screenshot out\shots\fe.png --shot-level 07000005 --wait 3 --input "none:4.5 enter:0.1 none:2 enter:0.1 none:2 enter:0.1 none:1.5 enter:0.1 none:1.5 down:0.1 none:0.6 enter:0.1 none:1.5 enter:0.1"
```

### Textos do PC no lugar dos do PS2

No PC não há cartão de memória, nem portas de controle, nem multitap. Os textos do jogo que falam desse hardware ("Saving
data. Do not remove memory card (8MB) (for PlayStation®2) in MEMORY CARD slot 1, reset, or switch off the console.")
dão lugar a um texto do PC, em inglês ou em português (`[ui] language_pc`), por uma tabela pequena só do port
(`kLabelOverrides` em `src/engine/pc_text.cpp`, `PcText_GameLabel`): `Txt_BindLabel` a consulta com o `USATxt.dat`
carregado, antes de devolver a string do arquivo, então a troca vale nas caixas do estilo Original e nos diálogos
Modern (que leem o texto da caixa). O arquivo do jogo não muda. Duas travas: a string do jogo tem de ter o trecho
esperado (`match`; outra versão do arquivo, com outros rótulos, passa direto) e o texto do PC só pode usar um prefixo
das conversões dela, porque o jogo o usa como formato do `snprintf` com os argumentos do original (o slot `"1"` e os
82 KB; `%.0s` consome o slot sem imprimir). O `nf_loader_check` confere a tabela no arquivo de verdade (`textos do PC:
25 de 25 rótulos do USATxt.Dat trocados`) e `tests/test_text.cpp` testa a troca, a passagem direta e as travas.

| Rótulo | O jogo (resumido) | No PC (inglês) |
|---|---|---|
| `0x267` | No memory card ... inserted into MEMORY CARD slot %s. You must insert ... %dKB ... | Save data cannot be written. At least 82 KB of free disk space is needed to save your progress. |
| `0x268` | Insufficient space on memory card ... %dKB ... console's internal browser. | Not enough free disk space. Free at least 82 KB to save your progress. |
| `0x269` | Memory card ... is unformatted. Format memory card ...? | The save folder is not set up. Set it up now? |
| `0x26a` | Insufficient space on memory card ... NightFire requires %dKB ... | Not enough free disk space. NightFire needs 82 KB free to save data. |
| `0x26b` | No memory card ... in MEMORY CARD slot %s. | The save folder could not be found. |
| `0x26c` | Saving data. Do not remove memory card ..., reset, or switch off the console. | Saving data. Please do not close the game. |
| `0x26d` | Save failed! Check memory card ... and please try again. | Save failed! Check the save folder and please try again. |
| `0x26e` | Save failed! Memory card ... error! | Save failed! The save folder could not be written. |
| `0x26f` | Autosaving data. Do not remove memory card ... | Autosaving data. Please do not close the game. |
| `0x271` | No NightFire save data present on memory card ... | No NightFire save data found. |
| `0x272` | Memory card ... is unformatted. | The save folder is not set up. |
| `0x273` | Load failed! Check memory card ... and please try again. | Load failed! Check the save folder and please try again. |
| `0x274` | Load failed! Memory card ... error! | Load failed! The save data could not be read. |
| `0x275` | Loading data. Do not remove memory card ... | Loading data. Please do not close the game. |
| `0x277` | Checking memory card ... Do not remove memory card ... | Checking save data… |
| `0x278` | Memory card ... error! Data may be corrupt. | Save data error! The data may be corrupt. |
| `0x279` | Are you sure you wish to format memory card ...? | Erase all save data in the save folder? |
| `0x27a` | Formatting memory card ... Do not remove ... | Setting up the save folder. Please do not close the game. |
| `0x27c` | Format failed! Please check memory card ... and try again. | The save folder could not be set up. Please check it and try again. |
| `0x3e6` | Deleting data. Do not remove memory card ... | Deleting data. Please do not close the game. |
| `0x3e7` | Delete failed! Check memory card ... and please try again. | Delete failed! Check the save folder and please try again. |
| `0x3c0` | Insert analog controller (DUALSHOCK®2) into controller port %s | Connect controller %s |
| `0x3c9` | analog controller (DUALSHOCK®2) is not inserted into controller port %s | Controller %s is disconnected. |
| `0x10000ba` | Insert multitap (for PlayStation®2) into controller port 1 | Connect a controller for each player. |
| `0x10001df` | Please reconnect the multitap (for PlayStation®2) to controller port 1 and press START button to continue. | Please reconnect the controllers and press START to continue. |

As mensagens que o `PS2_DoCardFlow` mostra são `0x267`-`0x27c` (sem `0x26e`, `0x26f`, `0x271`, `0x272`, `0x274`,
`0x279`, que ficam na tabela por serem do mesmo grupo) e `0x3e5`-`0x3e7`; o aviso de controle desconectado
(`MenuManager_Monitor`, página 0x4000002f) usa `0x3c9` e `0x10000ba`. Ficam como estão os que não falam do hardware
("This codename already exists. Would you like to overwrite it?", "Are you sure you want to delete this codename?",
"Save Successful") e os textos de outras versões que o código do PS2 não mostra ("Memory Card in slot A", "Xbox",
"GAMECUBENAME"). Os títulos e etiquetas só do port seguem a mesma ideia: o título Modern da caixa do cartão é "Save data"
(`PcText::FeCardTitle`), a etiqueta de um codinome gravado na roda é "Saved" (`FeTagCard`) e a lista sendo lida diz
"Reading save data…" (`FeListing`).

A caixa da partida (`P_PS2MEMCARDINIT`, página 0x40000048) segue o fluxo e o tempo do jogo: com a pasta sempre pronta, a
conferência termina no primeiro quadro (resultado 4) e a caixa não chega a ficar na tela; se ficasse, diria "Checking save
data…".

## Laço principal

`src/main.cpp` segue a estrutura do PS2 (`docs/engine.md`):

- O relógio do Windows gera o `VBlankCount` a 60 Hz (`psi_update_clock`).
- A cada volta, os quadros de VBlank desde o último tick (no máximo 2) viram um tick de lógica;
  `GS_SetRefreshRate(60 / ticks, 60)` publica a taxa, como no `main` original.
- O tick chama o `GameFlow_Main` portado: boot, carga da fase e, no estado de jogo, o `Game_Run`
  (objetos, portas, colisão, câmeras). O boot pede o menu principal (07000048): sem `--level` ele carrega e o
  jogo segue pelos menus dele (ver "Menu principal"); com `--level` o `main` troca o pedido pela fase da linha de
  comando. Durante uma carga (`GF_LOAD_MAP`) o `main` não desenha nem mexe na câmera: as estruturas da fase
  anterior já foram apagadas.
- O desenho do `GameFlow_Main` passa por um gancho (`g_gameflow_hooks.draw`) que só marca o pedido:
  o `Game_Draw` roda uma vez depois de cada tick que pediu desenho, como no PS2 a 60 quadros por segundo, e o
  registro dele vai para a interpolação entre ticks (ver abaixo); cada volta do laço desenha um quadro de vídeo
  entre os dois últimos registros. Com `--no-interp` (`[video] interpolation = off`) o `Game_Draw` roda em toda
  volta, na taxa do monitor, como antes.
- A entrada: a cada volta o `main` copia o estado do teclado e o movimento do mouse para
  `g_psi_keyboard_mouse`; o `Input_Update` de cada tick (no `Game_Run`) os transforma nas ações do
  jogo (`psiInput_MapInputs`) e o `Player_Update` as usa.
- A câmera: jogando, é a do jogo (`Player_PositionCamera`, atualizada pelo `Camera_UpdateAll` a cada
  tick); o `main` só põe o campo de visão e a proporção da janela. Na câmera livre, o `main`
  escreve a matriz dela na câmera 0 a cada volta e a atualiza sem a função de acompanhamento.
- PgUp e PgDn pedem a fase por `ResetMap_LevelToLoad`, sem o filme de abertura. Pacotes pedidos assim sem
  nada para desenhar (a tela de carga, o menu principal sem menu aberto) são pulados.
- Na câmera livre, Espaço aciona a porta à frente da câmera (até 4 unidades), como se o jogador a
  usasse (só do port).
- O som anda com os VBlanks: a cada lote mandado ao driver do IOP a SPU em software mistura 800 amostras por VBlank
  passado (ver "Som"). O `VBlankCount` vem do relógio (o do Windows, ou o simulado de `--fixed-video-hz`) e o
  `SFXUpdate` roda no `psiPostGame_Run` de cada tick: o som fica preso ao tempo real e aos ticks, nunca aos
  quadros de vídeo. O trovão dos relâmpagos do céu, que o `View_DrawSky` pede, agora conta ticks (antes contava
  quadros de vídeo e vinha mais cedo acima de 60 Hz).

## Interpolação entre ticks

Só do port (`src/render/frame_interp.h`, `src/main.cpp`; opções `[video] interpolation`, `instant_mouse_look` e
`fps_cap`). A lógica continua a 60 Hz como no PS2, e o `Game_Draw` roda uma vez por tick: o que ele guarda de um
quadro para o outro (os relâmpagos e o trovão do céu, o céu que gira da 0700001b, o `GlobalBlur`, o
`GameState.draws`, os números aleatórios que ele sorteia: o tremor das células com a flag 4, o giro do clarão) anda
no ritmo do PS2 em qualquer monitor. Antes ele rodava a cada quadro de vídeo: a 144 Hz o céu girava 2,4 vezes mais
rápido e o sorteio do `Rand` dependia da velocidade da máquina (até as capturas variavam: a morte de 07000012 com
`--input "none:10 space:0.3" --wait 12.5` às vezes não chegava ao menu).

**O registro.** De cada tick ficam a saída do `RuntimeGfx::collect_frame` (os `DrawItem`, com a paleta copiada) e a
câmera em uso (a matriz do `viewer_tag` e o campo de visão; a câmera 4 numa cena com câmera). Os dois últimos
registros ficam no `FrameInterpolator`.

**O quadro.** Cada quadro de vídeo mostra o instante `alpha` entre o tick anterior e o atual, `alpha` = a fração do
tick em curso que já passou (`psi_clock_vblanks()` menos o VBlank do tick, dividido pelos VBlanks entre os
registros: 1, ou 2 com a lógica a 30 Hz). A imagem fica um tick (16,7 ms) atrás do jogo. Interpolados:

- a câmera: posição em linha reta, rotação por slerp (quatérnio das linhas da matriz), campo de visão em linha reta;
- os modelos rígidos: a matriz decomposta em translação, rotação e escala por linha, interpolada e recomposta (as
  linhas saem ortogonais; nas pontas, a matriz exata do tick);
- os modelos com skinning: cada osso da paleta, do mesmo jeito;
- os pesos dos morphs dos rostos, em linha reta (ver "Rostos").

Ficam os do tick atual: as partículas (`psiDrawParticleList`, já viradas para a câmera do tick; a neve e as bolhas
também), os estilhaços, o 2D (HUD, menus, legendas, que nunca se interpolam), a névoa, as luzes, o tweak e o mapa de
ambiente (estado de desenho do jogo, que muda pouco de um tick para o outro). Os esmaecimentos da tela misturam o alfa
quando os dois registros têm um na mesma posição da lista (`FadeSlot`). Os menus Modern continuam desenhados a cada quadro, com o
movimento deles em tempo real.

**Os pares.** Cada desenho tem uma identidade (`DrawItem::interp`): o dono que o `psi_draw` grava
(`psi_draw_set_owner`, posto pelo `View_DrawObjList` para cada sala, célula e objeto, pelo `View_DrawSky` para cada
elemento do céu e pelos anexos, as marcas de tiro), o modelo, o `obj_tag::created` e a ordem do desenho dentro do
dono. O tick atual procura cada desenho no anterior por ela; sem par (objeto novo, identidade repetida, outro
modelo, outra quantidade de ossos), fica o do tick atual. Um par que salta mais de 2 m ou gira mais de 45 graus num
tick (um teleporte, o clarão do tiro, girado ao acaso a cada tick) também fica o do tick atual.

**Cortes.** Sem interpolação nenhuma (o quadro inteiro é o do tick atual): o primeiro tick depois de uma carga (o
recomeço da missão inclusive), a câmera de script ligando ou desligando, a câmera andando mais de 1,5 m ou girando
mais de 45 graus num tick, o campo de visão mudando mais de 25%. Na cena de abertura da 07000001 a câmera corta oito
vezes em 35 s, e no fim dela duas: a câmera do jogador volta e o jogador vai ao marcador da cena.

**Visibilidade.** Os portais e as listas continuam por tick (o jogo decide o que se vê). A câmera interpolada fica
entre as duas dos ticks; girando ou atravessando um portal, ela veria um pedaço do que o tick anterior via e o atual
não vê mais. Por isso o quadro também leva os desenhos do tick anterior sem par que ainda valem: salas e células
sempre (geometria parada), objetos só se o jogo ainda os tem vivos e desenháveis nessa câmera (com o mesmo
`created`), e só de donos que o tick atual não desenha mais (um destrutível que trocou de modelo, ou uma sala que o
canal trocou, não aparece com os dois), entre os da lista deles (a 0 no fim dela, a 1 no começo). A lista 2 (a
arma), as partículas e o 2D não. Num giro rápido com o mouse (378 graus/s) em 07000012, 77 de 600 quadros levaram de 1 a 6 desses.

**O mouse sem atraso** (`[video] instant_mouse_look`, ligado por padrão). O mouse giraria a visão um tick atrás,
e em degraus: o movimento chega a cada quadro de vídeo, mas o jogo o lê de 2 ou 3 quadros por vez (a 144 Hz). Então,
só ao desenhar (o estado do jogo não muda), a câmera interpolada gira mais o giro que o mouse aplicou no tick atual
e a interpolação ainda não mostra ((1 - alpha) dele, o `g_player_mouse_look` que o `Player_Update` grava) e o
movimento que chegou depois do tick (`psiInput_PeekMouseLook`, que o próximo `Player_Update` vai aplicar): o giro em
volta do eixo y do mundo e a elevação no plano frente-cima da câmera, presa a +-90 graus como o
`Player_ViewClamping`. Os desenhos presos à câmera (a lista 2: a arma em primeira pessoa e o clarão) giram junto,
em volta do olho. Só em primeira pessoa com o jogador olhando pelo mouse (`Player_PortMouseLookGate`: vivo, ativo,
fora de pausa, `cam_mode` 0; o giro só nos subestados que andam e sem trava, a elevação nos que miram), sem câmera
de script, sem menu e fora da câmera livre. Com `mlook=800,0` a 144 Hz o giro anda 0,700 grau por quadro, sempre;
sem ele, 0,875, 0,759, 0,584, 0,584... e uns 2 graus atrás. Limite conhecido: a visibilidade é a do tick, então no
canto da tela para onde se gira um objeto ou uma sala atrás de um portal que acaba de entrar pode aparecer um tick
depois (a faixa tem o giro de um tick: ~3 graus a 180 graus/s); alargar o frustum do jogo mudaria a lógica (o
`obj_tag::unknown_fc` dos vistos há pouco, o `Drone_PositionVisible`).

**Câmera livre.** F2, `--cam` e as fases sem jogador: a câmera livre anda a cada quadro de vídeo e é desenhada como
está (não se interpola); os objetos se interpolam.

**Até 60 Hz.** Com um quadro de vídeo por tick (a média de quadros por tick abaixo de 1,08, com histerese até 1,15),
`alpha` é 1: cada quadro é o último tick, sem o atraso, como antes (a 60 Hz não há o que interpolar; a deriva entre o
monitor e o relógio só faria o atraso variar). Nas capturas sem `--fixed-video-hz`, também (a imagem é a do tick).

**O limite de quadros** (`[video] fps_cap`: `off`, 30, 60, 120, 144, 165, 240 ou outro número; `--fps-cap`). Depois
do `present`, o laço espera o prazo do próximo quadro (`next_frame_deadline`: um período depois do anterior, sem
acumular erro; atrasado mais de um período, recomeça) com um temporizador de alta resolução do Windows e o resto em
volta. Junta-se à sincronia vertical. Com 30, cada volta tem dois VBlanks e a lógica passa para o modo de 30 quadros
do PS2 (`GS_SetRefreshRate(30, 60)`, passos dobrados), como o PS2 sob carga.

**Opções.** Nas duas páginas de opções (VIDEO > MOTION no Modern; a aba VIDEO da Original, que passa a rolar com oito
linhas): "Frame rate limit", "Frame interpolation" e "Instant mouse look", aplicadas com as outras e gravadas em
`[video]`. Desligada a interpolação, volta o comportamento antigo (o `Game_Draw` a cada quadro de vídeo), para
comparar.

**Medidas** (`ritmo` no relatório; `frames.tsv`). O custo na CPU não depende da resolução: em 07000013 e 0700000c a
3840 x 2160 e 240 Hz, 0,003 a 0,007 ms por quadro de vídeo (o pior 0,22 ms) e 0,03 a 0,07 ms por tick para guardar o
registro e achar os pares (o pior 0,29 ms), com 130 itens, 110 pares e 175 ossos.

Verificações (capturas em `out/interp/`, com `--fixed-video-hz` e `--frames-dump`):

- 07000012 virando com o controle (`--input "none:1 pad_rx=0.6:3" --wait 2 --fixed-video-hz 144`): o giro da câmera
  anda 0,0861 grau por quadro, sempre; sem a interpolação, 0,207 grau a cada 2 ou 3 quadros;
- 07000013 (`--wait 10`): o guarda que vem pelo saguão anda 0,0256 m por quadro a 144 Hz, sem degraus nos ticks;
- 07000001, a cena de abertura: nos cortes a câmera troca de plano de uma vez (o quadro do tick do corte já é o plano
  novo, sem mistura);
- 0700001b e 0700000a: o céu que gira e os relâmpagos contam o mesmo a 60, 144 e 240 Hz (600 passos do céu em 5 s, 5
  relâmpagos em 30 s); sem a interpolação a 144 Hz, 1438 passos e 6 relâmpagos;
- o 2D (`--only-2d`) é idêntico ao do caminho sem interpolação no mesmo tick, em qualquer alpha;
- `--interp-alpha 1` a 144 Hz dá a imagem do tick (igual à de 60 Hz); `--no-interp` dá as imagens do `main` anterior.

Para o DLSS (próximo passo): a mesma identidade dá a matriz do quadro anterior de cada desenho (os vetores de
movimento); faltam o jitter da câmera por quadro, os vetores de movimento das partículas e dos estilhaços (uma
máscara reativa) e o viés dos mipmaps.

## Caminho Direct3D 9 para o RTX Remix

Só do port, opcional e em protótipo (`--renderer d3d9ff`; `src/render/remix_d3d9.h`; o estudo inteiro, com as fontes, os
números e os passos para testar, está em `docs/remix.md`). O RTX Remix (path tracing, materiais PBR, DLSS) só intercepta
Direct3D 9 de função fixa; este caminho desenha a mesma lista de itens do Direct3D 11 (o `collect_frame`, ou o quadro
interpolado) do jeito que o Remix a reconhece. O Direct3D 11 continua o padrão e não muda (as capturas dele são
idênticas às de antes).

- **Recursos uma vez por fase.** O `RuntimeGfx` passa cada textura e malha criada na carga também a um espelho
  (`GfxMirror`, `render/runtime_gfx.h`), com o mesmo slot de `Tex` e o mesmo id do `Renderer`. O espelho Direct3D 9 cria
  vertex e index buffers `D3DPOOL_MANAGED` (FVF; índices de 16 bits quando cabem) e texturas A8R8G8B8 com mipmaps feitos
  na CPU e o alfa do GS dobrado (0x80 vira 0xff). Cada parte de malha é um `DrawIndexedPrimitive` com a faixa exata de
  vértices que usa: o hash de geometria do Remix fica estável.
- **Câmera e matrizes.** `SetTransform` de visão (`XMMatrixLookToRH` da câmera desenhada), projeção comum (z de 0 a 1,
  sem a profundidade invertida) e mundo por desenho.
- **Skinning.** Vertex blending indexado da função fixa: `D3DFVF_XYZB2 | D3DFVF_LASTBETA_UBYTE4`, `D3DVBF_1WEIGHTS`,
  `D3DTS_WORLDMATRIX(n)` = a paleta do jogo. Mais de 9 matrizes (o maior osso do disco é o 72) vai por processamento de
  vértices em software (o dispositivo é misto).
- **Cor e estado.** Textura x cor do vértice x 2 (`D3DTOP_MODULATE2X`, a escala do GS) e o tweak num segundo estágio
  (`D3DTA_TFACTOR`); mistura, teste de alfa e Z pelo estado do GS de cada parte (a mesma resolução do Direct3D 11, com a
  profundidade comum). Sem as rotinas da VU1: as luzes dinâmicas não iluminam a imagem rasterizada (vão ao Remix como
  `D3DLIGHT9`), e o mapa de ambiente usa as coordenadas da malha (a geração da função fixa estragou os desenhos seguintes
  no Direct3D 9 nativo). A névoa do GS vira névoa linear por vértice nos desenhos antes da passada de névoa.
- **A lista 2.** A limpeza do Z que o jogo pede antes dela (o item `clear_depth`, ver "A arma em primeira pessoa e a
  limpeza do Z") vira um `Clear(D3DCLEAR_ZBUFFER)`; com `--d3d9-viewmodel <z>` a lista 2 vai também ao viewport de
  profundidade 0 a `z`. A linha `d3d9ff` do relatório conta as limpezas (`limpa_z`).
- **2D.** Depois de todo o 3D, com projeção ortográfica e sem Z (o que o Remix trata como UI). Só o 2D original: o modo
  força o estilo de menu Original e desliga a camada 2D HD (as duas são Direct2D, no Direct3D 11).
- **Janelas.** A janela do jogo fica com o Direct3D 9; o Direct3D 11 (as camadas 2D usam o dispositivo dele) desenha numa
  janela invisível e não apresenta. O `d3d9.dll` só é carregado neste modo (`LoadLibrary`): com o Remix na pasta do
  executável, é o da ponte dele. O dispositivo usa `D3DCREATE_FPU_PRESERVE`. O back buffer não acompanha a janela.
- **Adaptador.** Com `[video] adapter`/`--adapter` diferente de `auto`, o Direct3D 9 usa o adaptador da lista dele com o
  mesmo fabricante e modelo (`GetAdapterIdentifier`: `VendorId` e `DeviceId`) que o Direct3D 11 escolheu; o Direct3D 9
  só lista adaptadores com monitor, então um integrado sem monitor (o desta máquina) não está lá e fica o padrão
  (`D3DADAPTER_DEFAULT`, como antes). A linha `d3d9ff` do relatório termina com `adaptador N "nome"` e se seguiu a
  escolha. O Remix só roda em placas NVIDIA RTX, então na prática o caminho fica na dedicada.
- **Capturas.** `--screenshot` lê o back buffer (`GetRenderTargetData`). Use `--fixed-video-hz 60` ao comparar com o
  Direct3D 11: o `Present` em janela segura o laço em ~60 quadros e a lógica pode dar dois ticks numa volta. O relatório
  ganha a linha `d3d9ff` (dispositivo, malhas e texturas espelhadas e recusadas, as partes desenhadas por tipo, luzes,
  view model, névoa, o `d3d9.dll` carregado). `--d3d9-remix-conf` grava um `rtx.conf` sugerido (escala, eixos e os hashes
  das texturas do 2D, do céu, das marcas e das partículas; dados do jogo, fora do git); `NF_D3D9_DUMP=<pasta>` grava as
  texturas dos dois caminhos por slot.

## Adaptador de vídeo e benchmark

Só do port (`src/platform/gpu_adapter.h`, `src/render/gpu_adapters.h`, `src/platform/bench_stats.h`, `src/main.cpp`,
`tools/bench_matrix.py`). A meta do usuário (10/10/2026): 60 fps em GPUs integradas com as opções padrão, sem
upscaler.

### O adaptador

`[video] adapter` no `nightfire.ini`, `--adapter` na linha de comando e a linha **Graphics adapter** em VIDEO das duas
páginas de opções escolhem a placa que desenha:

- `auto` (o padrão): o adaptador padrão do sistema, exatamente o caminho de antes (`D3D11CreateDevice` sem adaptador,
  `D3D_DRIVER_TYPE_HARDWARE`), que segue a "Preferência de gráficos" do Windows para o executável;
- `integrated`: entre as placas integradas, a primeira da ordem de menor consumo do Windows
  (`EnumAdapterByGpuPreference(DXGI_GPU_PREFERENCE_MINIMUM_POWER)`);
- `discrete`: entre as dedicadas, a primeira da ordem de alto desempenho (`HIGH_PERFORMANCE`);
- um número: o índice do `--list-adapters` (a ordem sem preferência, a de `EnumAdapters1`; até 3 dígitos);
- outro texto: a primeira placa cujo nome o contém, sem diferenciar maiúsculas (`radeon`, `rtx 5090`).

O adaptador de software (Microsoft Basic Render Driver) nunca é escolhido; sem nenhuma placa que sirva, fica o padrão e o
relatório diz `(nao achado: o padrao)`. Integrada é a placa de memória unificada que o próprio Direct3D informa
(`D3D11_FEATURE_D3D11_OPTIONS2.UnifiedMemoryArchitecture`, num dispositivo de teste por placa; só quando a preferência
precisa e no `--list-adapters`); sem a resposta, a que tem menos de 1 GiB de memória dedicada. Sem `IDXGIFactory6`, a
lista vem de `EnumAdapters1` e as ordens de consumo e desempenho valem a da lista.

O dispositivo do Direct3D 11 nasce no `IDXGIAdapter1` escolhido (`D3D_DRIVER_TYPE_UNKNOWN`), e a cadeia de troca vem da
fábrica dele. O Direct2D e o DirectWrite dos menus Modern e da camada 2D HD são criados a partir do mesmo dispositivo
(`IDXGIDevice`), então ficam na mesma placa. A linha `video` do relatório das capturas termina com
`adaptador N "nome" integrated|discrete pref P`.

A opção dos menus é pendente como as outras de VIDEO: o **Apply** a grava, mas o dispositivo é criado uma vez, então ela
só vale na próxima vez que o jogo abrir (o Modern avisa "Restart the game to use the new graphics adapter"; a descrição
da linha diz a placa em uso; na Original a linha se chama "Adapter (restart)" e mostra os nomes curtos, sem "(TM)", o fabricante e
"GeForce": "RTX 5090", "Radeon Graphics"). A lista oferece Automatic, Integrated, Discrete e cada placa pelo nome, uma vez por nome: a mesma placa pode
aparecer várias vezes no DXGI (a RTX 5090 desta máquina aparece com os índices 0, 2 e 3, só a 0 com o monitor), e o nome
escolhe a primeira. Duas placas iguais só se separam pelo índice, no ini ou no `--adapter`.

Desenhar numa placa que não tem o monitor (aqui: a Radeon integrada desenhando, o monitor na RTX) funciona: no `Present`
o DXGI copia cada quadro de uma placa para a outra, e esse custo aparece no tempo de quadro da janela, não no tempo de GPU
da integrada. Os modos do monitor saem da saída da outra placa (o renderizador procura o monitor da janela em todos os
adaptadores); a tela cheia exclusiva não é possível nesse caso (o DXGI recusa, e a janela fica como está). Num processo
de 32 bits o DXGI informa a memória dedicada limitada a uns 3 GB (a RTX 5090 aparece com 3072 MB).

### O benchmark

`--bench <s>` roda a fase como uma captura (só o `--input` move o jogador, o `nightfire.ini` só é lido com `--config`,
o cartão fica em memória, sem saída de som, mas o IOP e a SPU2 misturam do mesmo jeito), com o relógio de verdade (a
interpolação entre ticks com o alpha do relógio, como jogando), sem VSync e sem limite de quadros (`--bench-vsync`
mantém os das opções). A medida começa `--wait` segundos depois da carga (1 s se não for dado; `--shot-level` também
vale) e dura `s` segundos de relógio. O resultado sai no console (o `nightfire.exe` é um programa de janela: com a saída
redirecionada ele escreve nela; aberto de um `cmd`, liga-se ao console do pai) e é acrescentado a `--bench-out <txt>`:

```
bench fase 0700000c | adaptador 1 "AMD Radeon(TM) Graphics" integrated pref integrated | fora_da_tela 1920x1080 | cena 1920x1080 escala 100 msaa 1 filtro aniso8 hd_tex 1 (468/513) hd_ui 0 menus modern interp 1 vsync 0 limite off | config padrao
quadros 1409 tempo 8.00 s fps_medio 176.1 | quadro_ms media 5.678 p50 5.456 p99 9.299 (1% low 107.5 fps) p99.9 9.941 (0.1% low 100.6 fps) piores_1% 9.844 piores_0.1% 11.834 max 13.726
gpu_ms media 5.651 max 9.857 | cena 4.984 nevoa 0.496 resolucao 0.000 2d 0.170 desfoque 0.000 direct2d 0.000 | quadros 1409 descartados 0
cpu_ms por_tick logica 0.864 desenho_do_jogo 0.318 (60.0 ticks/s) | por_quadro interpolacao 0.013 envio 5.065 present 0.177
desenhos por_quadro itens_da_lista 64.6 itens_desenhados 64.6 chamadas 1914.0 | present tearing 1 coberto 0
```

- **Tempos de quadro** (`platform/bench_stats.h`): do fim de um quadro ao fim do seguinte. fps médio = quadros / soma
  dos tempos; p50, p99 ("1% low") e p99,9 ("0,1% low") pela posição mais próxima (`ceil(p/100 x N)` na lista em ordem),
  com o fps equivalente; e a média dos 1% e 0,1% mais lentos (a outra definição comum de "low").
- **GPU** (`Renderer::gpu_frame_begin/end`): consultas de carimbo de tempo do Direct3D 11 dentro de um
  `TIMESTAMP_DISJOINT` por quadro, num anel de 5 quadros lido sem esperar. Um carimbo marca o fim de cada trecho quando
  o tipo de desenho muda: `cena` (os itens 3D, as limpezas), `nevoa` (a passada de névoa), `resolucao` (a resolução do
  MSAA e a redução da escala), `2d` (o 2D do jogo e da camada HD), `desfoque` (o fundo dos menus Modern) e `direct2d`
  (o desenho dos menus Modern). Os carimbos contam o tempo de relógio da GPU, com as esperas: quando a CPU é o gargalo
  (a RTX), o número inclui a GPU parada entre os envios e não é o custo dela; numa GPU que é o gargalo (a integrada) ele
  é o custo.
- **CPU**: por tick, a lógica (`GameFlow_Main`) e o desenho do jogo (`Game_Draw`, `collect_frame` e o registro da
  interpolação); por quadro, a interpolação (`build` e o mouse sem atraso), o envio (`Renderer::render`, os menus Modern)
  e o `Present` (numa GPU que é o gargalo o envio inclui a espera por ela: 5 ms na integrada em 0700000c). **Desenhos**: os itens da lista e as chamadas `Draw`/`DrawIndexed` por quadro.
- **Janela ou fora da tela.** Sem `--offscreen`, o quadro vai à janela com o `Present` (a janela fica por cima das
  outras: coberta, o `Present` não mostraria nada, `DXGI_STATUS_OCCLUDED`, contado em `coberto`). Com `--offscreen`, numa
  textura do tamanho de `--width`/`--height`, sem `Present`: o custo da própria placa, sem a cópia para o monitor; para a
  CPU não correr à frente da GPU sem limite, cada quadro espera o de dois quadros atrás acabar (uma consulta `EVENT`, como
  a fila de uma cadeia de troca).
- **VSync desligado de verdade.** Sem VSync o `Present` agora usa `DXGI_PRESENT_ALLOW_TEARING` quando o sistema tem
  suporte (a cadeia de troca nasce com `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING`; fora da tela cheia exclusiva, onde o DXGI
  recusa a flag). Antes, numa janela composta pelo DWM o modelo flip ainda esperava a atualização do monitor e o "VSync
  desligado" ficava preso a 240 Hz neste monitor (o primeiro benchmark em janela dava exatamente 240,0 fps). Com o VSync
  ligado (o padrão) nada muda.
- `--bench-frames <tsv>` grava cada quadro (tempo, envio, `Present`, lógica e desenho do jogo do quadro), para achar os
  picos. `tools/bench_matrix.py` roda a matriz de cenas x placas x resoluções x janela/fora da tela e escreve uma tabela
  em markdown e a saída bruta de cada execução.

### Medidas (10/10/2026, esta máquina)

A máquina: NVIDIA GeForce RTX 5090 (o monitor, 240 Hz) e a AMD Radeon(TM) Graphics integrada do processador (1002:164e,
2 unidades de computação RDNA 2, memória unificada; uma das integradas mais fracas de hoje, sem monitor ligado). Opções de
um `nightfire.ini` novo (sem `--config`): escala 100%, sem MSAA, filtro anisotrópico 8x, texturas HD ligadas (o pacote do
PCSX2 de 2,8 GB está na pasta dos dados), interpolação entre ticks ligada, menus Modern, camada 2D HD desligada; o
benchmark tira o VSync e o limite. 8 s por medida, depois de 1 s de aquecimento (`--wait`); as fases girando o jogador no
lugar (`--input "mlook=700,0:120"`, uma volta a cada ~4,7 s), o tiroteio de 07000012 com os guardas atirando
(`--wait 5`, o jogador girando e atirando), a pausa e as opções Modern abertas por `--open-menu-page`, o menu principal
(Press START > o menu) e a tela de título. Medido em `d074740` + este trabalho (`python tools/bench_matrix.py`; a saída
bruta fica em `out/bench/`). Cada célula: fps médio / 1% low / 0,1% low.

| cena | GPU | 1080p fora da tela | 1080p janela | 720p fora da tela | 720p janela | GPU ms 1080p (fora da tela): trechos |
|---|---|---|---|---|---|---|
| 0700000c (girando) | iGPU | 176 / 108 / 101 | 144 / 86 / 82 | 354 / 206 / 176 | 296 / 167 / 135 | 5.65: cena 4.98 nevoa 0.50 2d 0.17 |
| 0700000c (girando) | RTX 5090 | 1821 / 479 / 308 | 2063 / 637 / 386 | 1989 / 549 / 341 | 1992 / 620 / 390 | 0.14: cena 0.13 nevoa 0.01 2d 0.00 |
| 07000014 (girando) | iGPU | 377 / 150 / 121 | 290 / 123 / 109 | 776 / 316 / 284 | 606 / 262 / 214 | 2.63: cena 2.52 2d 0.11 nevoa 0.00 |
| 07000014 (girando) | RTX 5090 | 4724 / 1186 / 575 | 3679 / 812 / 557 | 4683 / 1191 / 600 | 3823 / 862 / 589 | 0.04: cena 0.04 2d 0.00 nevoa 0.00 |
| 0700001b (girando) | iGPU | 365 / 273 / 251 | 292 / 199 / 178 | 688 / 490 / 304 | 543 / 364 / 318 | 2.72: cena 2.47 2d 0.25 nevoa 0.00 |
| 0700001b (girando) | RTX 5090 | 3639 / 1229 / 702 | 3093 / 871 / 649 | 3657 / 1228 / 689 | 3183 / 839 / 536 | 0.04: cena 0.04 2d 0.00 nevoa 0.00 |
| 07000012 tiroteio | iGPU | 319 / 198 / 49 | 255 / 151 / 53 | 638 / 400 / 154 | 502 / 295 / 74 | 3.10: cena 2.41 2d 0.69 nevoa 0.00 |
| 07000012 tiroteio | RTX 5090 | 5126 / 1171 / 540 | 3238 / 754 / 497 | 5441 / 1235 / 624 | 3546 / 754 / 414 | 0.05: cena 0.04 2d 0.01 nevoa 0.00 |
| 07000012 + pausa Modern | iGPU | 186 / 119 / 42 | 166 / 110 / 77 | 411 / 280 / 70 | 320 / 198 / 45 | 5.30: cena 2.71 desfoque 1.75 direct2d 0.84 |
| 07000012 + pausa Modern | RTX 5090 | 1552 / 686 / 480 | 1068 / 435 / 281 | 1553 / 673 / 452 | 1533 / 637 / 435 | 0.09: cena 0.05 desfoque 0.03 direct2d 0.01 |
| 07000012 + opções Modern | iGPU | 203 / 145 / 88 | 161 / 106 / 42 | 380 / 263 / 70 | 336 / 208 / 166 | 4.87: cena 2.39 desfoque 1.64 direct2d 0.84 |
| 07000012 + opções Modern | RTX 5090 | 1077 / 508 / 357 | 1130 / 552 / 406 | 1102 / 532 / 372 | 1141 / 553 / 376 | 0.09: cena 0.04 desfoque 0.04 direct2d 0.01 |
| menu principal Modern | iGPU | 263 / 195 / 179 | 205 / 118 / 108 | 523 / 361 / 317 | 388 / 226 / 201 | 3.76: desfoque 1.65 direct2d 1.35 2d 0.62 |
| menu principal Modern | RTX 5090 | 2730 / 1306 / 912 | 1962 / 986 / 704 | 2720 / 1251 / 831 | 1913 / 920 / 692 | 0.06: desfoque 0.03 direct2d 0.02 2d 0.01 |
| tela de título | iGPU | 835 / 618 / 478 | 521 / 290 / 254 | 1030 / 322 / 196 | 1025 / 531 / 427 | 1.18: 2d 0.58 direct2d 0.46 cena 0.14 |
| tela de título | RTX 5090 | 3212 / 1301 / 797 | 2049 / 888 / 628 | 3335 / 1376 / 905 | 1972 / 852 / 539 | 0.03: direct2d 0.02 2d 0.01 cena 0.00 |

- **A meta (60 fps na integrada com as opções padrão) está cumprida com folga**: o pior caso em média é 0700000c a
  1080p, 176 fps fora da tela e 144 na janela, com o 1% low em 108 e 86. A integrada faz de 2,4x (0700000c na janela) a
  14x (a tela de título) os 60 fps. Nenhuma opção padrão precisou mudar.
- **Onde vai o tempo da integrada** (carimbos de tempo, 1080p fora da tela): a cena 3D (0700000c: 5,0 ms de 5,65; ~1900
  chamadas de desenho por quadro), a névoa de 0700000c (0,5 ms, uma passada na tela inteira), o desfoque dos menus Modern (1,6 a
  1,8 ms: a cópia do alvo, a redução a 1/4, quatro passadas de gaussiana e a mistura) e o Direct2D deles (0,8 a 1,4 ms);
  o 2D do jogo custa 0,1 a 0,7 ms. A interpolação entre ticks custa 0,001-0,03 ms de CPU por quadro; a lógica 0,25-1,05 ms
  e o `Game_Draw` 0,05-0,5 ms por tick.
- **Os 0,1% lows abaixo de 60** (07000012 com o tiroteio, a pausa e as opções: 42-77 fps, uma dezena de quadros em
  8 s) são quadros isolados da integrada que levam 15 a 25 ms **na GPU**, num trecho qualquer (a cena num, o desfoque
  noutro), com os quadros vizinhos normais; o quadro seguinte espera por ele no envio (`--bench-frames` mostra: envio de
  18-25 ms logo depois de um quadro de GPU de 18-26 ms). Acontecem também com `--no-hd` (menos), não acontecem na RTX
  (0,1% low acima de 280 fps nas mesmas cenas) e não seguem a lógica do jogo: são da placa (relógio ou energia da
  integrada, ou outro cliente dela), não do port. Num monitor de 60 Hz com VSync seriam um quadro repetido de vez em
  quando.
- **Janela com o monitor na outra placa.** A integrada na janela paga a cópia de cada quadro para a RTX no `Present`:
  18-25% menos fps que fora da tela (0700000c: 144 contra 176) e o tempo de GPU da integrada sobe ~1 ms (a cópia disputa a
  memória). Num computador com o monitor ligado à integrada (o caso da meta: notebooks e desktops sem placa dedicada) o
  número que vale é o de fora da tela mais o `Present` normal. Com VSync (o padrão; `--bench-vsync`, janela de 1080p):
  a integrada fica em 135 fps em 0700000c e 149 na pausa (o monitor é de 240 Hz), a RTX em 240,0 (o monitor).
- **A RTX 5090** é limitada pela CPU em todas as cenas (0,03-0,15 ms de GPU por quadro fora da tela, 1000-5400 fps): o
  envio de ~1760 desenhos por quadro em 0700000c custa ~0,34 ms de CPU, a lógica e o `Game_Draw` a cada tick dão os quadros
  mais lentos (o 1% low). Na janela os carimbos de tempo da RTX incluem a GPU parada esperando a CPU (0,2-0,6 ms).
- Propostas, se um dia a folga apertar (nenhuma é necessária para a meta): o desfoque sem a cópia do alvo (a redução
  lendo o alvo direto e a mistura como blend com fator constante, `src x a + dst x (1 - a)`; idêntica com o desfoque
  inteiro, `a = 1`), economizando ~0,6 ms a 1080p na integrada; guardar os `IDWriteTextLayout` dos menus Modern entre
  quadros (CPU); o filtro trilinear ou anisotrópico 2x em placas integradas (muda a imagem, então só como padrão por
  placa, se medido).

## Esmaecimentos, rastro e tela de carga

O que o PS2 desenha entre a cena e a tela, com o código do jogo (o como em `docs/engine.md`, "Esmaecimentos, rastro e
tela de carga"):

- **Esmaecimentos.** O preto que volta no começo de cada fase (45 ticks), o preto de uma cena que acaba ou é pulada e a
  volta depois dela, o preto antes de recomeçar, sair, terminar a missão ou trocar de fase (a câmera 6 do `GameFlow`),
  os 20 ticks de preto antes de uma NIS carregável (o estado 0xd) e o clarão branco da granada de luz e do tremor
  (`Player_SetFlashBang`). Cada um é um quadrado da janela inteira na ordem do `Game_Draw`: o da câmera 0 fica embaixo do
  HUD, o da câmera 6 cobre tudo, menus Original incluídos (os Modern ficam por cima). Com a interpolação entre ticks o
  alfa é misturado entre os dois registros, sem degraus de 60 Hz a 144 ou 240 Hz.
- **O rastro.** O borrão de movimento que o PS2 faz misturando o quadro novo com o que está na tela (`ScreenBlur`): no
  clarão, numa troca de zoom e debaixo d'água (72% do quadro novo por tick). `Renderer::screen_blur` guarda o quadro
  anterior e o mistura com o peso por VBlank elevado à duração do quadro de vídeo, para o rastro durar o mesmo tempo em
  qualquer taxa; sem rastro pedido nada é copiado. Não entra a cor `SqueezeColourIn` dessa cópia (o PS2 clareia 13%).
- **A tela de carga.** Entre as fases (não no menu principal) aparece a tela do PS2: a imagem da fase, "LOADING", o
  nome da missão, uma dica sorteada e as bolinhas que vão e voltam. Numa janela ela é apresentada desde o começo da
  carga até o fim (o preto do `PS2WipeVram`, a tela e cada passo das bolinhas, a cada 11 VBlanks pelo relógio de parede);
  não há tempo mínimo, nem no PS2, então ela fica o tempo da carga: 0,2 a 0,7 s aqui (o relatório: `tela_carga ... ms`).
  Nas capturas e no `--bench --offscreen` nada é apresentado; `--load-screen <png>` grava a primeira tela de carga num
  PNG (`--load-screen-blob <n>` acende a cadeia n das bolinhas), e `--no-load-screen` não a monta (depuração).
- **A dor.** O vermelho do ferimento (`BLData::pain_flash`) já estava na tela: no PS2 não há tela vermelha, é o alfa das
  setas vermelhas da direção do tiro no HUD (pane 1, sprites 20 a 23).

### Pular uma cena

Enter ou Espaço no teclado (os comandos `pause` e `menu_select`, a ação 25 do Start), Start ou A no controle, durante
uma cena com câmera que pode ser pulada: a câmera 4 escurece em até 45 ticks enquanto o script corre até o fim, e a
câmera 0 volta do preto em 22 ticks. As cenas de morte não pulam, como no PS2, e a pausa não abre (o jogador está
desligado na cena). Captura: `--level 07000001 --fixed-video-hz 60 --input "none:12 enter:0.1" --wait 12.35` (a
abertura da 07000001 no meio do preto: `esmaecimento 1 | camera 4 ... alfa 119 ... passo 0.0222`).

### Capturas e relatório

| O quê | Comando (`--fixed-video-hz 60 --screenshot ...`) | Relatório |
|-------|----------------------------------------------------|-----------|
| começo de fase | `--level 07000012 --wait 0.3` | `esmaecimento 1 \| camera 0 cor 000000 alfa 153 estado 1` |
| recomeço depois da morte | `--level 07000012 --input "none:10 space:0.3 none:2.5 space:0.3" --wait 13.6` | `camera 6 ... alfa 152`, `recomecos 1` |
| fim da abertura | `--level 07000001 --wait 35.3` | `camera 4 ... alfa 121`, `camera_script 06000087` |
| clarão do tremor | `--level 07000016 --channel f5 --input "none:5 pad_rx=0.8:5" --wait 7.5` | `camera 0 cor ffffff alfa 95`, `clarao borrao 95 resta 150.0 total 150.0 rastro 104` |
| debaixo d'água | `--level 07000016 --at 2.27,-2.0,-94.70,0.0 --input "none:1.0 mlook=0,500:0.4 W:2.5" --wait 4` | `agua ... submerso 1`, `rastro 92` |
| tela de carga | `--level 07000012 --load-screen out/shots/carga.png --load-screen-blob 3 --wait 1` | `tela_carga fase 07000012 imagem 030000d6 bolinhas textura 13 16x16 cadeia 3` |

As linhas: `esmaecimento N | camera c cor rrggbb alfa a estado s tempo t passo p` (cada esmaecimento do quadro, alfa na
escala 0-255 do `Camera_SetFade`), `clarao borrao B resta R total T rastro S | tremores canal:segundos:clarão:ligado`
(o clarão do jogador 0, o `ScreenBlur` do último `Game_Draw`, os tremores da fase) e `tela_carga telas limpezas bolinhas
passos bombas ms textura imagem apresentadas`.

## Tempo e enfeites

A chuva, a neve, as bolhas, o vento das salas, os raios, as nuvens, as marolas, as folhas, as árvores animadas, as
hélices, as paredes de um lado e o bafo no frio, todos com o código do jogo (`src/engine/weather.cpp`,
`src/engine/scenery.cpp`; o como em `docs/engine.md`, "Chuva, neve, raios e nuvens" e "Enfeites"). Eram 206 dos 235
posicionamentos provisórios; sobram 29, os carros das arenas do multijogador e um da 07000046 (`Car_Create`). Pelas
fases (`nf_loader_check`: `tempo=tipo/máximo caixas= raios=pares/pontos nuvens= enfeites=`):

| Fases | O quê |
|-------|-------|
| 07000001 a 04, 09 a 0b, 41 | chuva (modelos de gota; 128 nas 09 a 0b), caixas de chuva |
| 07000005 a 08 | neve (4096 flocos em partículas), o bafo do jogador e dos guardas |
| 07000014 | bolhas que sobem |
| 07000001, 41 | raios entre dois pontos (só com o relâmpago do céu que repica) |
| 07000006, 41 | nuvens |
| 07000001, 02, 04, 0b, 41 | marolas; 07000002 e 04 também árvores, 07000004 e 41 folhas |
| 07000001, 09, 0a, 0c, 0d, 11 a 13, 41 | hélices e ventiladores |

Capturas (`--fixed-video-hz 60`):

| O quê | Comando | Relatório |
|-------|---------|-----------|
| chuva | `--level 0700000b --wait 4` | `tempo tipo 1 max 128 vivas 122 ... caixas 17 ativas 11`, `enfeites ondulacoes 5 aneis 845` |
| neve | `--level 07000005 --wait 4` | `tempo tipo 0 max 4096 vivas 4083 ... caixas 18 ativas 5`, `bafos 42` |
| raios | `--level 07000001 --force-lightning --cam -12,10,-25,5,18,-6 --input "none:1 enter:0.1" --wait 4` | `raios 11 pares 9 desenhados 480` |
| marolas | `--level 07000001 --cam -10,6.5,-1,-10,4.41,3.73 --input "none:1 enter:0.1" --wait 6` | `ondulacoes 7 aneis 84` |
| árvores e folhas | `--level 07000004 --cam -7.79,6.5,21,-7.79,6.5,27.39 --input "none:1 enter:0.1 none:3 enter:0.1" --wait 8` | `folhas 39 vivas 25 arvores 2` |

Os raios da 07000001 nunca aparecem no jogo: só são desenhados enquanto o céu mostra um relâmpago com repique
(`SuperHappyLightningFlag`), e o céu dela não tem esse relâmpago; `--force-lightning` (depuração) o liga a cada tick e
repica a cada 10. As partículas (neve, bolhas) e os modelos de gota são os do tick atual (não interpolados), como as
outras partículas.

Todos sorteiam com o `Rand` como no PS2 (96 sorteios por carga no `InitDrops`, um `Rand_Rand(20000)` por tick no
`Env_Update`, as marolas, as folhas, as árvores e a dica da tela de carga), então as capturas e as provas que dependem do
`Rand` mudam em relação às de antes (07000003: o pêndulo leva 1 tiro em vez de 2 na prova; na rodada de todas as fases
os sorteios se acumulam de uma fase para a outra). `--no-env` (depuração, no `nightfire.exe` e no `nf_loader_check`)
deixa o tempo e os enfeites provisórios e pula a tela de carga: as capturas voltam idênticas às do `main` de antes
(07000012 aos 8 s, 0700000b e 07000005 aos 4 s, 0700000c aos 3 s: 0 pixels; os menus Modern só diferem no hash da
compilação do rodapé) e as provas da 07000003 e da 0700000a também.

O custo (`--bench 10 --offscreen`, 1920 x 1080, girando com `mlook=700,0`, a Radeon integrada, contra o `main`):

| Fase | main | com o tempo | GPU (cena) |
|------|------|-------------|------------|
| 0700000b (chuva, marolas) | 353 fps | 204 fps (1% low 100) | 2,7 → 4,7 ms |
| 07000005 (neve) | 288 fps | 252 fps (1% low 153) | 3,1 → 3,6 ms |
| 0700000c | 245 fps | 241 fps | 3,6 → 3,7 ms |
| 07000001 (chuva) | 388 fps | 380 fps | 2,3 → 2,4 ms |

A meta de 60 fps na integrada continua com folga de 3,4 vezes ou mais. A chuva da 0700000b é a mais cara: ~230 itens a
mais por quadro (as gotas de modelo e ~150 anéis de marola vivos, cada um um desenho).

## Filmes

Os 22 filmes do disco (`MOVIES/30_FPS/*.PSS`, formato em `docs/formats.md`, "Vídeos") tocam onde o jogo os toca, pelas
funções do próprio jogo (`platform/psi_movie.h`): as duas aberturas (EA e MGM) antes do "Press START", o clipe da
Esthero depois dele (pula com Enter depois de 6 s), a demonstração depois de 45 s parado, o trailer de *Die Another Day*
(AV Options, no estilo Original), a abertura e o fim de cada missão (o mapa das missões pede a fase com o filme; o fim da
missão carrega o filme e depois os resultados), o making of depois da última missão e os dois fundos em laço das páginas
do menu principal (07330048 no menu principal, 07350048 nas outras). Uma missão começada pelo mapa toca a abertura com
as legendas do jogo por cima (o script do filme, criado pelo `ResetMap_Load` com as legendas ligadas); Enter (Start)
pula, como no PS2.

### Do arquivo à tela

- **O arquivo**: `PssFile` (`platform/pss.h`) lê o PSS aos pedaços de 64 KB e separa o vídeo elementar (stream 0xE0, em
  unidades de um quadro cada com os cabeçalhos de antes, `Mpeg2Units`) e o áudio (o private stream 1 sem os 4 bytes de
  cada PES; o cabeçalho SShd/SSbd e os blocos de 0x200 bytes desintercalados em estéreo, `PssAudio`).
- **O vídeo**: o decodificador de MPEG-2 do Windows, pelo Media Foundation (`platform/movie_decoder.h`), sem nada
  baixado: `MFTEnumEx` da categoria de decodificadores de vídeo para `MFVideoFormat_MPEG2` e o primeiro da lista que ativa
  e aceita o vídeo, saída NV12 na ordem de exibição (o MFT reordena os quadros B), na CPU. Nesta máquina (Windows 11,
  processo de 32 bits) a lista tem dois, os dois da Microsoft: o **"MPEG2VideoExtension"** (o pacote
  `Microsoft.MPEG2VideoExtension` 1.2.32 da loja, assinado pela Microsoft, que vem com o Windows) é o que funciona; o
  "Microsoft MPEG Video Decoder MFT" (`msmpeg2vdec.dll`, presente em `SysWOW64`) recusa a ativação com `c004f011`
  (`SL_E_PRODUCT_SKU_NOT_INSTALLED`, o licenciamento do MPEG-2 do Windows). Sem nenhum que sirva, o filme não abre (como
  um arquivo que falta: as páginas seguem na hora, como antes deste porte) e o stderr diz por quê. Decodificar um quadro
  de 512 x 448 e convertê-lo leva ~0.75 ms (máximo ~2 ms). A conversão para BGRA (`nv12_to_bgra`) é a do CSC do IPU:
  BT.601 de faixa limitada com os coeficientes em 1/128 do IPU, o croma de cada bloco 2 x 2 sem interpolar, o alfa 0x80.
- **O som**: o PCM entra na entrada de dados do núcleo 0 da SPU2 (`Spu2::input_push`; no PS2, a transferência em bloco
  de `audioDecStart`), vezes o `BVOL` que `changeInputVolume` põe (`MovieVolume` x 100: com o volume dos efeitos em 100,
  0x2710 = 0.305, -10.3 dB), no seco e no reverb do núcleo 0 pelo `MMIX` 0xff0, e sai pela mistura de sempre
  (`audio_out`: o XAudio2, o WAV de `--audio-dump` ou nada). O volume é o que o jogo pede ao abrir o filme
  (`SFXGetVolume`): mudar os efeitos durante um filme só vale no próximo, como no PS2. Os fundos do menu não têm som.
- **A imagem**: o renderizador começa o quadro pelo filme (`render/movie_layer.h`, como a cópia da imagem para o buffer de
  quadro que o PS2 faz antes do resto do desenho, `PS2UploadBGMImage`): o alvo da cena preto e a imagem na tela do PS2 (a
  área 4:3 de `psi_2d_viewport`, então barras pretas dos lados numa janela larga), ampliada com um filtro bicúbico de
  Catmull-Rom (9 leituras bilineares; nítido em 1080p e 4K). O que o jogo desenha depois (as legendas, as páginas do menu
  no estilo Original) fica por cima. A névoa não é desenhada com um filme tocando (`psiDrawFogPass`, como no PS2).

### O ritmo e a sincronia

Com um filme tocando, o `main` roda a lógica só nos VBlanks pares (30 ticks por segundo, `GS_SetRefreshRate(30, 60)`) e
chama `psiDecodeBackgroundMovieFrame` antes de cada tick, como o `main` do PS2; entre os ticks, `psiSwapBGFMVFrame` lê o
arquivo adiante. No PS2 o vídeo anda um quadro por tick e o som pelo relógio da SPU2, sem ligação entre os dois (o vídeo
começa logo, o som depois de 0x6000 bytes no IOP).

No PC o vídeo segue o som (só do port): o quadro mostrado a cada quadro de vídeo é `floor(ouvido / 1600)` (30 fps a 48
kHz), onde *ouvido* é o som do filme que a SPU2 já consumiu menos o que ainda está na fila do XAudio2
(`audio_out_queued_frames`), isto é, o que já saiu da caixa (`MovieClock` em `platform/pss.h`). Num filme sem som (os
fundos do menu), ou depois que o som acaba (um vídeo uns quadros mais longo que o áudio), o relógio é o da própria SPU2
(as amostras que ela misturou e já se ouviram, 800 por VBlank). O som e a imagem começam juntos no primeiro quadro (o
primeiro PTS dos dois é o mesmo no disco). O fim do filme é o do vídeo, como o `sceMpegIsEnd` do PS2: o tick em que o
relógio passa do último quadro.

Medidas:

- **Num WAV** (`--audio-dump`, 07000001 com `--intro-movie`): a correlação do dump com o áudio do arquivo
  (`tools/movies.py demux`) dá 0.9998 com o atraso de 0 amostras e o ganho 0.3051 (o `BVOL` 0x2710 é 0.3052): a amostra
  `1600 k` do WAV é a do quadro `k`.
- **No aparelho** (`NF_MOVIE_LOG=<arquivo>`: por quadro de vídeo, o consumido, a fila do aparelho, o ouvido, o quadro do
  relógio e o mostrado): numa janela com o XAudio2, o relógio ouvido anda 47 978 amostras por segundo (o relógio da placa
  de som), a 2.4 ms de uma reta na mediana (6.2 ms no p99), com a fila do aparelho em 23 ms na mediana; o quadro
  mostrado é sempre o do relógio (o decodificador acompanha).

### Os menus Modern

O menu principal Modern tem o fundo animado: o fundo em laço da página do jogo (o mesmo `07330048`/`07350048` do PS2)
fica no lugar da arte do título, no mesmo retângulo 4:3 na altura da janela, atrás do mesmo desfoque e do véu
(`FrontScreen::set_movie`, `ui::kMovieImage`: a imagem vem da textura do filme do renderizador). O título ("Press
START") continua com a arte (a página do jogo não tem filme). Um filme de tela cheia (as aberturas, o clipe, a
demonstração, o fim do jogo) passa por cima: a tela Modern não desenha nada e não prende a entrada, então Start pula como
na página do jogo (`Menu_StopMovie`, que só pula os filmes com `movie_skip` depois de `movie_counter` ticks: as aberturas
não pulam). O trailer fica só no estilo Original (o AV Options do Modern é a categoria AUDIO).

### Opções e capturas

- `[video] movies = on|off` no `nightfire.ini` (padrão on); `--movies`/`--no-movies` por cima. Desligados, o filme não
  abre e o jogo segue como sem ele.
- As capturas (`--screenshot`) e o `--bench` começam **sem** filmes (as capturas de antes continuam iguais pixel a pixel):
  `--movies` os liga. `--movie-frame <n>` liga os filmes e captura no quadro n do filme que tocar (`--movie <hash>`: só
  desse); o `--wait` e o `--shot-level` não contam. `--intro-movie` faz o `--level` pedir a fase com o filme de abertura,
  como o mapa das missões. Nas capturas o relógio do filme é o da SPU2 sem aparelho: um quadro por tick, determinístico.
- O relógio do `--input`/`--wait` é `GameState.ticks / 60`: durante um filme a lógica roda a 30 ticks por segundo, então
  ele anda à metade do tempo real (as aberturas do menu, 14.2 s, contam ~7 s do roteiro).
- O relatório da captura tem a linha `filme <código> tocando laco quadro mostrado/vistos relogio decodificados som
  consumido faltas bvol | abertos fim parados recusados ligados decodificador ms media/max`; o `frontend_moderno` diz
  `filme 1` quando o fundo animado está atrás da tela; o `--bench` acrescenta `filme <código> cpu_ms por_quadro |
  decodificacao media max`.

```bash
./build/Release/nightfire.exe --movie-frame 48 --movie 07370048 --screenshot out/shots/ea.png   # a abertura da EA
./build/Release/nightfire.exe --level 07000001 --intro-movie --movie-frame 330 --screenshot out/shots/m1.png  # a abertura da missão, com as legendas
./build/Release/nightfire.exe --level 07000004 --win 1 --movie 07300004 --movie-frame 450 --screenshot out/shots/fim.png  # o fim da missão
./build/Release/nightfire.exe --input "none:19 enter:0.1 none:8 enter:0.1" --movie 07330048 --movie-frame 90 --screenshot out/shots/fundo.png  # o menu principal sobre o fundo animado
./build/Release/nightfire.exe --level 07000001 --intro-movie --movie-frame 900 --audio-dump out/audio/m1.wav --screenshot out/shots/m1b.png  # o som do filme num WAV
python tools/movies.py list        # os filmes; info <hash>, demux <hash> <pasta> (o .m2v e o .wav em out/)
```

O custo (10/10/2026, `--bench 8`, 1920 x 1080): o filme da abertura de 07000001 roda a 857 fps fora da tela e 637 numa
janela no **Radeon integrado** (a GPU desenha o quadro em ~1.0 ms: o preto e a imagem com o Catmull-Rom), 8105 no RTX
5090; o menu principal Modern sobre o fundo animado, 244 / 212 (como sem o filme: o desfoque e o Direct2D dominam), o
Original 615 numa janela. Na CPU, decodificar e converter um quadro leva 0.74-0.87 ms em média (2.2 ms no pior), 30 vezes
por segundo: 0.03-0.12 ms por quadro de vídeo. A meta de 60 fps no integrado sobra.

O que fica de fora: o caminho Direct3D 9 do Remix não desenha os filmes; o trailer no estilo Modern (o AV Options do
Modern é a categoria AUDIO, sem a linha do trailer); a memória do filme, que no PS2 sai do heap do jogo
(`AllocateMovieMemory`, ~2.3 MB) e no PC não.

## Som

O jogo toca o som com o seu próprio código (`Sound_*` e `SFX*`, `docs/engine.md` "Som") e o driver do IOP portado
(`src/platform/sfx_iop.cpp`, o `SFX.IRX`) sobre uma SPU2 em software (`src/platform/spu2.cpp`). Os dados vêm da pasta
`PS2\` do disco, ao lado do `FILES.BIN` (`--data`): os bancos `SB_n`, as falas (`STREAMS`) e a música (`MUSIC\MFX_n`);
nada é convertido nem copiado (formatos em `docs/formats.md`, "Áudio"). A saída (`src/platform/audio_out.cpp`):

- **o aparelho** (o padrão do jogo): XAudio2 2.9 (`xaudio2.lib`, do Windows 10 em diante), estéreo de 16 bits a 48 kHz;
  a primeira mistura vai 50 ms adiantada e a fila guarda até 8 pedaços (se o jogo atrasar mais que isso, falta som: a
  contagem `starved`; com a fila cheia o pedaço é descartado, `dropped_chunks`). Sem aparelho de som o jogo segue mudo;
- **um WAV** (`--audio-dump out/a.wav`): os mesmos quadros num arquivo (o cabeçalho é reescrito a cada segundo, então
  um WAV de uma execução interrompida ainda abre);
- **nada** (`--no-audio`, e sempre nas capturas com `--screenshot` sem `--audio-dump`, no `nf_level_check`, no
  `nf_loader_check` e no `nf_tests`): o driver e a SPU rodam iguais (o jogo vê os mesmos sons vivos), só a mistura é
  descartada.

A mistura é **determinística**: ela não segue o relógio do Windows, e sim o `VBlankCount` do jogo; antes de cada RPC a
SPU anda 800 amostras por VBlank passado (no máximo 600 VBlanks de uma vez). Uma captura com `--audio-dump` dá sempre o
mesmo WAV, e o som de uma captura começa no boot (a tela de carga) e vai até a imagem.

O relatório das capturas tem a linha `audio <aparelho|wav|mudo> vozes N tocando N falas N banco SB_n reverb N pausa P
cena C | jogo pedidos N tocados N falas N nos N | iop pedidos N comecados N recusados N ausentes N roubados N | saida pico
dB rms dB frames N`: as vozes da SPU que soam, os itens vivos no driver, as falas tocando, o banco carregado, o reverb
da sala, a pausa e a cena; os pedidos do jogo (`Sound_Play*` com nó, os que chegaram ao IOP, as falas) e os nós em uso;
os pedidos do driver (começados, recusados pelos limites, ausentes do banco, roubados por prioridade); o pico e o RMS
da saída nos últimos 0.5 s. A linha seguinte, `musica fase F faixa 0xT trecho N pedido P ataque A fim W`, é a
música do EE (a linha de `MapMusic` em uso, a faixa, o trecho que o IOP diz tocar, o último pedido, o combate e o fim
da música da fase). O `nf_loader_check` imprime por fase `sons: pedidos= tocados= falas= legendas= recusados=
ausentes= banco=SB_n vozes= gatilhos_som= gatilhos_musica= musica=faixa/trecho` (a carga e os 2 s de jogo). `NF_SFX_LOG=<arquivo>` registra cada pedido do jogo
(`ee Sound_Play3D 0x2c5 em x,y,z`), cada som que o driver começa (tempo, som, handle, posição em mm, raios, ouvinte),
as recusas e a música (`musica 0x16 -> MFX_8 trecho 13`, os saltos).

Para olhar um WAV sem ouvir: `python tools/sound.py analyse out/audio/a.wav --window 1` (pico, RMS de cada canal,
cruzamentos por zero e cortes por janela). Outras ferramentas do `tools/sound.py`: `banks`, `sfx <banco>`, `wavbank`
(as amostras de um banco em WAV), `streams`/`streamwav` (as falas), `music`/`musicwav` (os marcadores e as faixas),
`sfxtable` (o `SFXOutputData` do ELF) e `irxsyms` (os símbolos do driver). Os WAV vão em `out/` (dados do disco: nunca
no git).

O que o PC reproduz do PS2, e onde diverge:

- o volume: `ES_CombineVolumes` e o 3D ao quadrado, a fase invertida da voz de trás (o surround Pro Logic: em fones
  ou caixas estéreo, um som atrás do ouvinte soa "largo"), o modo mono da AV Options (sem inversão);
- as 48 vozes, o limite de 15 vozes de efeitos (menos durante rajadas de sons), o roubo por prioridade e o
  `MaxVoices`;
- o reverb "studio C" do `LIBSD.IRX`, pelo reverb da sala da câmera;
- a pausa (as vozes congelam), as cenas (sons pausados e o fade no fim) e os fades dos menus;
- a interpolação do pitch é cúbica de 4 pontos (a SPU usa uma gaussiana de 4 pontos: o timbre dos sons tocados
  abaixo de 48 kHz muda um pouco);
- os dois núcleos da SPU2 e o caminho do núcleo 0 ao 1, com as saturações em 16 bits de cada soma (ver abaixo);
- um item que o driver recusa (o limite de vozes: com as vozes cheias o som novo de menor prioridade rouba a si
  mesmo) fica na lista até o fim do update seguinte, que o trata como os outros; num item polifônico a fila de amostras
  (`CuedSamples`, `CueSampleWaiting`) ainda não foi preenchida e o IRX começaria a "amostra" do endereço 0 do IOP (num
  item nunca usado) ou a da vida anterior do item. O port não começa uma amostra fora do banco do item e conta a vez em
  `g_sfx_iop_stats.guards` (`guardas_iop=` no `nf_loader_check`, `guarda: amostra fora do banco` no `NF_SFX_LOG`). Era
  a queda de 0700000a: os trovões (0x5d/0x5e, duas amostras, prioridade 95) pedidos a cada quadro de vídeo com
  `--no-interp` enchiam as vozes;
- a saída passa por um bloqueio de DC (o capacitor da saída do PS2);
- as falas são lidas inteiras na hora (o PS2 as lê do CD aos poucos) e a música é lida do disco aos poucos pela linha
  do tempo dos marcadores; um salto de música que espera o próximo marcador o acha depois do que já tocou (o PS2,
  depois do que já leu, até ~2 s adiante).

### O nível da saída

Um tiroteio satura a saída, e isso é do PS2, não falta de uma atenuação no port. A conta (`formats.md`, "A mistura
da SPU2") tem ganho 1.0 da voz até o DAC: o driver põe o volume mestre dos dois núcleos em 0x3fff (o máximo do
formato de 15 bits), o `sceSdInit` do `LIBSD.IRX` liga a saída do núcleo 0 ao núcleo 1 com `AVOL` 0x7fff, o volume
pedido pelo jogo é ignorado (100) e o dos efeitos é 100; os filmes também põem o mestre em 0x3fff. Um efeito cheio no
centro sai pela metade em cada lado (~-6 dB de pico, com as amostras normalizadas dos bancos), e alguns juntos passam
do limite. O que o port corrigiu ao conferir: a SPU2 virou dois núcleos (cada soma de 24 vozes satura em 16 bits, a
saída do núcleo 0 entra no 1, um reverb por núcleo) e o volume do retorno do reverb (`EVOL`) é de 16 bits: o máximo do
driver (0x3332) vale 0.4, o port usava 0.8 (o reverb 6 dB alto; nas salas com reverb, como 07000009, a diferença no RMS
total é de menos de 0.1 dB).

Medido (`--audio-dump --fixed-video-hz 60`, o relógio simulado que torna o dump repetível, e `python tools/sound.py
analyse`; um dump também escreve no stderr `spu2: saturacoes vozes0 N vozes1 N nucleo0 N saida N pico_sem_corte X dB`,
onde a SPU2 saturou e o maior valor que a saída teria sem o corte final):

- 07000012 parado na partida, 14 s com os guardas atirando (`--level 07000012 --wait 14`): picos de 0 dBFS, 341
  amostras cortadas no WAV (de 1.35 milhão), RMS de -11 a -14 dB nos 5 s de tiroteio; a soma do núcleo 0 saturou 453
  vezes e a saída 811, e sem o corte final o pico seria +3.7 dB. Na mesma linha do tempo, a SPU2 de antes (uma soma
  só, o reverb 6 dB alto) cortava 352 amostras e o RMS total é o mesmo (-16.3 dB; a diferença entre os dois WAV fica a
  -62 dB). Com a música muda os efeitos sozinhos ainda cortam; a música sozinha fica em -20 a -27 dB RMS.
- A abertura de 07000001 (10 a 34 s, as falas de Kiko e Mayhew): falas e ambiente a -27.2 dB RMS, a música sozinha a
  -38.8 dB: a fala fica 11.6 dB acima da música, o que o jogo pede (a música a 70, ao quadrado 49%; a fala com o volume
  do banco).
- 07000007, um convidado a 2 m dizendo "Enjoy the party." (`NDrone2_BondTalk`): a fala a -19.3 dB RMS sobre o ambiente
  e a música a ~-26 dB.

Um limitador no fim da saída do PC não ajudaria: a saturação acontece antes, na soma das vozes do núcleo 0. Para não
cortar seria preciso misturar sem as saturações da SPU2 e limitar no fim, o que já é outra coisa que o PS2 (o
limitador abaixa a mistura inteira por dezenas de ms em volta de cada pico, onde o PS2 corta poucas amostras). Com
excessos de no máximo ~4 dB, em menos de 0.03% das amostras de um tiroteio, a recomendação é manter o corte fiel e não
ligar limitador; se um teste com caixas e fones de verdade achar o corte áspero, um modo opcional (`[audio]`, desligado
por padrão: muda o som) misturaria em 32 bits e limitaria a saída.

## Campanha a pé: travessia e aparelhos

As fases a pé têm os objetos que os objetivos pedem, com o código do jogo (docs/engine.md, "Campanha: o que falta para
completar" e as seções seguintes): escadas, gancho, cordas, tirolesas, paredes de esgueirar e os ícones de toque
(`src/engine/player_traverse.cpp`), cadeados de teclado, laser e decodificador, o Q-Worm e os alvos da micro-câmera
(`ssys.cpp`), torretas (`turret.cpp`), helicópteros (`copter.cpp`) e os objetos de roteiro simples (`simple_script.cpp`).
No teclado: andar para a frente encostado numa escada sobe (W), pular perto do ícone de uma corda ou tirolesa pega o
fio (espaço), usar perto de uma parede de esgueirar encosta (E); os aparelhos trocam com 1 e 2 (próximo e anterior),
o botão direito mira (a câmera e o gancho pedem mira) e o esquerdo atira.

Capturas que mostram cada um (o relatório `.txt` tem `jogador x y z giro r subestado s`, `arma`, `canais` e
`missao`):

```bash
# escada: de y 27,10 a 34,63 (subestado 1 no meio, aos 3 s)
nightfire.exe --level 07000005 --at 59.43,27.10,89.21,1.571 --input "none:0.3 W:7" --wait 8 --fixed-video-hz 60 --screenshot out/shots/escada.png
# tirolesa: subestado 15 com a câmera de terceira pessoa, 11,8 m abaixo no fim
nightfire.exe --level 0700000c --at 46.83,21.53,-205.50,0 --input "none:0.5 space:0.05" --wait 1.5 --fixed-video-hz 60 --screenshot out/shots/tirolesa.png
# micro-câmera ("4", o quarto aparelho), mirar e fotografar: canal 0x1c (28, o objetivo)
nightfire.exe --level 0700000c --at 5.31,7.38,-112.12,0.002 --input "none:0.5 4:0.1 none:1.5 rmb:1.5 rmb+lmb:0.3 rmb:1" --wait 5.5 --fixed-video-hz 60 --screenshot out/shots/foto.png
# Q-Worm ("5", o quinto aparelho), olhar para baixo com o controle simulado e atirar no computador: canal 0x4c (76)
nightfire.exe --level 0700000b --at -19.71,47.88,30.70,2.710 --input "none:0.5 5:0.1 none:1.8 pad_ry=-1:0.25 none:1 lmb:0.3 none:3" --wait 8 --fixed-video-hz 60 --screenshot out/shots/qworm.png
```

O `nf_loader_check` prova o resto em cada fase (a linha `campanha: ... travessia: ...`), e suas coordenadas servem de
`--at`.

## Segurança: câmeras, monitores, holofotes, minas e o resto

Os objetos que faltavam às fases a pé, com o código do jogo (docs/engine.md, "Câmeras de segurança, monitores e
holofotes" e as seções seguintes): as câmeras de segurança, os monitores e as contagens do sistema de segurança
(`src/engine/sensor.cpp`), os holofotes (`searchlight.cpp`), as minas, os atiradores e o tremor (`hazard.cpp`), o
inversor (`trigger.cpp`), o canhão montado (`gunimp.cpp`) e o laser e os mísseis da estação espacial
(`space_station.cpp`). Antes, os posicionamentos deles eram provisórios (desenhados parados, sem lógica): só os guardas
davam alarme e as fases ficavam mais fáceis. Os provisórios de todas as fases caíram de 441 para 270, sem mudar
`inimigos=` em nenhuma fase.

**A imagem do monitor.** O PS2 não desenha a imagem de uma câmera numa textura nem num pedaço da tela: usar o monitor
(E) põe a câmera do próprio jogador (a 0) na matriz da câmera de segurança, a cada tick, sem a função de atualização
dela, para o jogador (`Player_Disable`) e troca os painéis 0 e 1 do HUD pelo 16 (os cantos vermelhos e o "REC"
piscando, `HUD_UpdateSecCamPane`). Então o port não precisou mexer no renderizador: a câmera 0 já é a desenhada (com a
interpolação entre ticks, que corta quando a câmera pula mais de 1,5 m, e sem o olhar instantâneo do mouse, que só vale
com o jogador ativo). O modelo do monitor não mostra imagem nenhuma no PS2 também. O painel 11 (a câmera espiã, os
marcadores sobre os inimigos) já estava portado e não é desta parte. As câmeras especiais 5 a 10 continuam fora do
renderizador (só o 2D delas entra), mas nenhum objeto deste trabalho as usa.

Capturas (1024x576, `--fixed-video-hz 60`; o relatório tem a linha `seguranca cameras v/s/r/d alarmes N monitores N
mostrando x,y,z(grupo_g) holofotes v/s/q/a minas N explodidas N canhao ... misseis a/s/l` e `canais`):

```bash
# 0700000a: a câmera do corredor vê o jogador (estado 1), o alarme liga (canal 0x1e) e, pelo gatilho OU, o 0x12, que
# acende a torreta automática do teto: aos 9 s ela desceu e atira (efeitos marcas 33; o main: nenhum canal, nada)
nightfire.exe --level 0700000a --at 10.67,1.28,17.79,-1.686 --wait 9 --fixed-video-hz 60 --screenshot out/shots/camera.png
# 07000014: E no monitor do grupo 1 mostra a câmera em 26,32 3,54 29,00 em tela cheia com o painel 16; E de novo passa
# à próxima câmera do grupo (26,62 3,54 30,93) e, depois da última, devolve o jogador
nightfire.exe --level 07000014 --at 47.45,1.1,35.6,0 --input "none:0.5 E:0.1" --wait 1.5 --fixed-video-hz 60 --screenshot out/shots/monitor.png
nightfire.exe --level 07000014 --at 47.45,1.1,35.6,0 --input "none:0.5 E:0.1 none:1 E:0.1" --wait 2.5 --fixed-video-hz 60 --screenshot out/shots/monitor2.png
# 07000006: os quatro holofotes acesos pelos canais da história (--channel), um acha o jogador parado no caminho da
# mancha: seguindo (holofotes 2/2/0/0), o alarme 0x37 ligado e o brilho de frente para a câmera
nightfire.exe --level 07000006 --at 6.40,41.01,-43.95,-1.23 --channel 3d --channel 3e --channel 3f --channel 40 --wait 6 --fixed-video-hz 60 --screenshot out/shots/holofote.png
# 07000015: a mina de laser da porta, mirada com o mouse e um tiro de besta: a explosão (dano 80, raio 15) leva o
# jogador de 100 a 32,9 de vida
nightfire.exe --level 07000015 --at 13.49,41.4,73.68,-2.338 --input "none:0.5 mlook=0,277:1 none:0.3 lmb:0.1 none:2" --wait 2.25 --fixed-video-hz 60 --screenshot out/shots/mina.png
```

`--channel <hex>` (depuração, repetível) liga um canal logo depois da carga, para capturar o que espera a história
(os holofotes de 07000006 acendem com os canais 0x3d-0x40). O `nf_loader_check` prova cada objeto por fase (a parte
`seguranca:` da linha `campanha:`): câmeras que viram o jogador parado na frente delas e ligaram o alarme, e o que reagiu
em 5 s (personagens que mudaram de alerta ou de estado, os nascidos dos geradores, as torretas que saíram do repouso e
os canais que ligaram, cada um com quem o espera); monitores mostrando as câmeras do grupo; holofotes achando o jogador
na mancha, seguindo-o e iluminando ali (a luz da mancha entre as de `Lights_CalcClosestLights`); minas explodindo; o
canhão montado, atirando, superaquecendo e desmontado; e os objetos de canal (atiradores, inversores, tremores e a
estação: cargas e tiros do laser, mísseis armados e lançados).

O desempenho não muda (`--bench 8 --offscreen`, 1920x1080, a Radeon integrada; o main e este trabalho): 07000005 293,6
e 294,7 fps; 07000006 com os holofotes acesos 343,6 (parados, no main) e 337,0; 07000014 372,1 e 376,9, e olhando por um
monitor 325,7 (o painel 16 custa 0,4 ms de 2D).

## Polimento da jogabilidade: mira, mira automática, laser, água e corpos rígidos

O que faltava ao jogo a pé, com o código do jogo (docs/engine.md, "Mira do tronco", "Jogador" (a água), "Armas do
jogador" (a mira automática, o laser e o relógio), "Corpos rígidos" e "Colisão"):

- **A mira do tronco** (`AnimObjectAimAt`, `AnimGetBoneWorldTrans2`): os inimigos que miram giram a coluna, e com ela
  os braços e a arma, para o cano apontar para o jogador; com o jogador no alcance, `DroneWeap_Ready2Fire` passa a
  esperar o cano apontar (20 graus) antes do tiro, como no PS2. No jogo de um jogador só os personagens miram assim (o
  corpo do jogador mira só no multijogador). `--no-torso-aim` (depuração) desliga, para comparar.
- **A mira automática** (`Check_AutoAim`): a opção "Auto aim" (GAMEPLAY no Modern, Advanced Options no Original; ligada
  por padrão, como no PS2) agora age: a cruz, a arma e a bala vão para o peito do inimigo dentro de um cone de 10 graus
  x a mira automática da arma x a distância (até 35 m), maior no Operative, nenhum no 00 Agent.
- **O laser e o relógio** (`Player_LaserPointer`, `Player_SetWatch`, `Draw_LaserBeam`, `Draw_TaserBeam`): o raio do
  laser do relógio do cano até onde acerta (o que queima os cadeados de laser já era dos projéteis), o mostrador e os
  ponteiros do relógio na hora da missão, a mira laser das pistolas melhoradas (o feixe fraco e o ponto vermelho) e o
  raio em zigue-zague do taser. São modelos 3D esticados, como no PS2, não triângulos soltos nem partículas.
- **A água** (`Camera_Shear`, `Env_WaterRipple`): debaixo d'água a câmera ondula (a matriz dela com um cisalhamento de
  2,5% que gira) e pede o borrão da tela (`GlobalBlur` 0x91, o rastro: ver "Esmaecimentos, rastro e tela de carga"); os passos na
  água rasa soltam marolas. Nadar, mergulhar, o fôlego no HUD e o afogamento já estavam portados.
- **Os corpos rígidos** (`RB_*`): os 34 objetos 0xd8 das fases a pé (lanternas de papel e peças penduradas) eram
  provisórios parados; agora balançam com o vento e com os tiros (com marcas de bala) e voltam à vertical. Em repouso
  ficam onde estavam (as capturas sem tiro dão a mesma imagem).
- **O resto da colisão**: `Collide_CylinderIntersect` (a cápsula dos pêndulos) e `Collide_PlaceObject` com
  `GT_DeployMiniGun` (a metralhadora desmontada de uma torreta volta a ser montada onde o jogador atira com ela).
  `AnimGetCollData` (raios e esferas contra as juntas dos personagens) já estava portada com o corpo da IA.

**A mira automática e o mouse (a escolha do PC).** O jogo do PS2 não tem mouse (nem código de mouse no ELF) e não
desliga a mira automática por aparelho: as regras dele são a opção, mirar (o gatilho de mira desliga a procura e a
mira suavizada segue a manual), o gancho (0x50/0x51), que a liga sempre, e a dificuldade (00 Agent a desliga). No PC a
mesma regra vale para o mouse e o controle: o botão direito do mouse é o mirar do jogo e desliga a mira automática como
no PS2, e quem prefere a mira livre desliga a opção (GAMEPLAY > Auto aim; `[game] auto_aim = off` no `nightfire.ini`).
Com a opção ligada, uma captura com inimigos à vista não é mais igual à de antes (a cruz e a arma vão para o alvo);
com ela desligada, é igual pixel a pixel.

Capturas (1024x576 ou 2560x1440, `--fixed-video-hz 60`; o relatório tem as linhas `mira_ia mirando N no_alcance N
guardas N | perto ... giro_tronco inclinacao flags erro_cano`, `mira_auto ligada alvo dist tela x y`, `laser ligado
feixe alfa comprimento vistas segmentos ponto x y z vistas escala relogio desenhados`, `corpos_rigidos N tipos ...
criados tiros batidas carregados capsula | perto ... inclinacao giro vel` (`NF_LIST_RB=1` lista todos) e `agua nivel
olhos cabeca submerso ar ondulacao borrao`):

```bash
# a mira do tronco: 07000012, o jogador agachado (C) à frente de dois guardas; o cano do mais perto erra o jogador por
# 0,5 grau (giro_tronco -0,037, inclinacao 0,098); com --no-torso-aim, por 6,3 graus
nightfire.exe --level 07000012 --at 22.49,1.09,19.155,-1.68 --input "none:0.5 C:0.1" --wait 5 --fixed-video-hz 60 --screenshot out/shots/tronco.png
# a mira automática: o mesmo lugar, cinco tiros pelo controle simulado (RT); com [game] auto_aim = on num --config,
# 2 acertos (1 abatido); com off, 0 (o main antes: 0). Aos 4,62 s a cruz está no peito do guarda agachado (alvo 2,
# tela 0,092 -0,208) e o tiro acertou; desligada, a cruz fica acima da cabeça
nightfire.exe --config out/aa_on.ini --level 07000012 --at 22.49,1.09,19.155,-1.68 --input "none:4.5 pad_rt:0.1 none:0.3 pad_rt:0.1 none:0.3 pad_rt:0.1 none:0.3 pad_rt:0.1 none:0.3 pad_rt:0.1" --wait 7 --fixed-video-hz 60 --screenshot out/shots/mira_auto.png
# o laser do relógio no cadeado de 07000008 ("2", o segundo aparelho, põe o laser, arma 78): o raio do relógio ao
# cadeado, o mostrador com os ponteiros (relogio 3); o main não desenhava raio nem ponteiros
nightfire.exe --level 07000008 --at 160.24,43.10,-117.14,1.571 --input "none:0.5 2:0.1 none:1.9 lmb:3" --wait 4 --fixed-video-hz 60 --screenshot out/shots/laser.png
# a mira laser da pistola melhorada (--unlock-all dá a arma 8): o feixe (alfa 31) e o ponto vermelho na escada a 2 m
nightfire.exe --unlock-all --level 07000005 --at 59.43,27.10,89.21,1.571 --wait 2 --fixed-video-hz 60 --screenshot out/shots/mira_laser.png
# a lanterna de papel de 07000003: três tiros (mouse para cima), depois o jogador anda de lado e olha: a lanterna balança
# (tiros 3, inclinou 15 graus logo depois; --frames-dump mostra o balanço) com as marcas das balas; no main, parada
nightfire.exe --level 07000003 --at 1.95,6.43,-20.5,0.0 --input "none:0.3 mlook=0,-700:0.25 none:0.4 lmb:0.1 none:0.3 lmb:0.1 none:0.3 lmb:0.1 none:0.1 D:0.9 mlook=-1300,0:0.3" --wait 3.2 --fixed-video-hz 60 --screenshot out/shots/lanterna.png --frames-dump out/shots/lanterna --frames 40
# debaixo d'água em 07000016 (o jogador cai na água, olha para baixo e nada): submerso 1, o ar caindo (88,6), a
# ondulação andando e o borrão 0x91 pedido
nightfire.exe --level 07000016 --at 2.27,-2.0,-94.70,0.0 --input "none:1.0 mlook=0,500:0.4 W:2.5" --wait 4 --fixed-video-hz 60 --screenshot out/shots/agua.png
```

O `nf_loader_check` prova cada parte em cada fase a pé (a parte `polimento:` da linha `campanha:`, docs/engine.md
"Depois do porte") e lista as armas com laser, raio, taser, relógio e mira automática (`armas: mira_laser=6,7,8,9,f,17,18,
raio_laser=4e,4f, taser=4a,4c, relogio=38,4e,4f,5a,5c, mira_auto=...`). Os provisórios de todas as fases caem de 270
para 235 (os corpos rígidos), com `inimigos=` igual em todas; os tiros da IA mudam um pouco (`tiros_ia`), porque os
inimigos agora esperam o cano apontar.

## Mira moderna e roda de armas

Duas coisas só do PC, decididas com o usuário em 10/10/2026: mirar pela alça com o mouse (a mira moderna, ADS) e a roda
de armas segurando Tab. As duas ficam em cima do código do jogo: o modo de mira, o zoom, a luneta, a troca de arma e as
animações continuam os do jogo.

### A mira moderna (`src/engine/player_ads.h`)

`[game] aim_style = auto|modern|classic` (`platform/pc_gameplay.h`; `--aim-style`; GAMEPLAY > "Aim style" no Modern e a
última linha da aba CONTROLS no Original). `auto` (o padrão) é o moderno enquanto o último aparelho usado é o teclado e o
mouse (`psiInput_ActiveDevice`) e o do jogo com o controle; `classic` é sempre o do jogo.

No estilo moderno o botão de mirar continua sendo a ação 19 do jogo (a mesma regra "Manual aim" Toggle/Hold, que por
padrão é segurar; a mira automática continua desligada mirando, como no PS2), e:

- **Armas de luneta** (`weapon_definition_tag::flags` 0x40: os fuzis de precisão, a besta 0x11, o OICW 0x1a, o laser
  0x32, a câmera 0x54): a luneta do jogo (as panes 6, 11, 17, 18 e 19 do HUD) e o zoom do jogo, que a roda do mouse anda
  (`psiInput_SetWheelZoom`). Duas correções do port valem aqui nos dois estilos: o zoom do jogo (`BLData::zoom`, que a
  câmera leva em `viewer_tag::lod_scale` e o PS2 multiplica na escala da projeção, `PS2WorldViewVU1Kick2`) agora estreita
  o campo de visão desenhado (antes o port o ignorava: a luneta não aproximava nada); e numa janela mais larga que 4:3 a
  luneta ganha faixas pretas dos lados (o PS2 a desenha na tela inteira; aqui a área 4:3 em peças, e o mundo aproximado
  apareceria fora dela).
- **Armas de zoom sem luneta** (o zoom máximo acima de 1: 0x18/0x19 e o lança-granadas 0x2a/0x2b, com uma luneta no
  modelo em cima da linha de mira) e os **lança-foguetes** (0x2c-0x31, o tubo no ombro): a visão do jogo, com a arma no
  quadril (com o zoom, aumentada, como no PS2), mas a mira presa no centro (o mouse gira a visão).
- **As outras armas de fogo**: segurar o botão leva a arma, em ~0,17 s (10 ticks, uma curva suave), da pose do quadril
  para a pose de mira, com a alça e a massa sobre o centro da tela; um zoom leve (1,25 nas pistolas, 1,2 na espingarda,
  1,35 nas submetralhadoras e fuzis) só no desenho e na sensibilidade do mouse (`mouse_aim_sensitivity` dividida pelo
  zoom do jogo vezes esse); a mira do jogo presa no centro (`Player_Aiming` trata a arma como a de luneta: as setas e o
  analógico giram a visão, `aim_x/aim_y` ficam 0, e o tiro sai do centro pelo `Player_GetAimingPoint` de sempre); a cruz
  do HUD some (`HUD_UpdateCrossHair`). Recarregar, trocar de arma ou de variante tira a arma da pose (o modo de mira do
  jogo continua; ela volta sozinha no fim, com o botão ainda seguro), como nos jogos modernos: as animações de recarga do
  jogo foram feitas para o quadril. O coice e as animações de tiro continuam (a arma pula do mesmo jeito); o balanço de
  andar cai a 25%.

**A pose.** A arma na mão é um objeto animado: os braços são uma malha com skinning (ossos 0-37: o peito, os braços, as
mãos e os dedos) e a arma são modelos rígidos presos aos ossos 38 em diante (`ANIMSKIN_tag::discrete`). A linha de mira
de cada pele (a alça e a massa no espaço do objeto da arma, na pose do primeiro quadro da animação de espera) está em
`src/engine/player_ads_table.inc`, medida da geometria do jogo por `nf_loader_check <dados> --miras`
(`tests/sights_measure.cpp`): os vértices das partes no corredor do cano (o anexo 0 da pele, o do clarão; a segunda arma
das duas mãos e as peças guardadas fora dele ficam de fora), numa faixa de 4 cm para cada lado do eixo e da mão direita
menos 10 cm para a frente (a coronha atrás dela não é linha de mira); o perfil de cima (o mais alto em 64 faixas ao longo
do cano), o casco convexo dele e a aresta quase horizontal (inclinação até 0,2) mais longa, com pelo menos 1/4 da arma ou
20 cm. A faixa larga é de propósito: o alto do ferrolho dos modelos do PS2 é uma face com vértices só nas bordas, e uma
faixa só do meio perdia o alto da arma. A pose (`ads_pose`) gira a arma para a linha da alça à massa apontar para a frente
(sem rolar: o "cima" da arma fica para cima) e a põe com a alça na profundidade em que ela fica no quadril, x 0,85 (presa
a 0,3-0,8 m), vezes o zoom total (o leve e o do jogo: com a visão aumentada Z vezes, a arma Z vezes mais longe fica do
mesmo tamanho na tela). `ads_tick`, no fim de `Player_WeaponRecoil`, mistura a pose do jogo (`gun->pos/rot`) com a de
mira; `ads_pose_arms`, depois do `AnimObjectUpdate` da arma, desce e recua o peito, os braços e parte dos antebraços
(ossos 0, 1, 19, 37 inteiros e 2, 20 a 60%, 0,3 m) no referencial do olhar: com a arma no centro a pose do quadril poria
o ombro e a manga na frente do olho; assim os antebraços sobem de baixo até as mãos, que continuam na arma.

As linhas medidas (14 peles, todas com alça e massa achadas):

| Armas (weapon_data) | Pele | Zoom | Como fica |
|---|---|---|---|
| 0x02/0x03, 0x04/0x05 (pistolas) | 05000088, 050000a9 | 1,25 | o fundo do ferrolho com a alça no centro |
| 0x06/0x07, 0x08/0x09, 0x46 (P2K) | 05000047, 050000a8 | 1,25 | idem |
| 0x0a-0x0d | 05000051 | 1,35 | o fundo da arma com a alça |
| 0x0e/0x0f, 0x10 (Raptor) | 05000040, 0500003f | 1,35 | o alto do ferrolho no centro (a alça é uma face plana) |
| 0x12/0x13 | 05000048 | 1,35 | aproximada: a linha da parte da frente (a de trás é baixa) |
| 0x14/0x15 | 05000050 | 1,35 | a massa no centro; a alça (um aro) aparece abaixo dela |
| 0x16/0x17 | 050000c0 | 1,35 | o aro de trás no centro, a coronha branca embaixo |
| 0x1c/0x1d (espingarda) | 0500004a | 1,2 | o alto da caixa até a ponta do cano |
| 0x43/0x44 (dardos) | 050000a3 | 1,35 | o fundo da arma |
| 0x42, 0x45 (só multijogador) | 05000091, 050000b6 | — | medidas, não conferidas em captura |

Aproximadas (a linha passa pelo alto do modelo, não pelo entalhe da alça, que os modelos do PS2 quase não têm): todas um
pouco; mais a 0x12 e a 0x14 (a alça fica abaixo da linha). Quando o achador não acha aresta (nenhuma das 14 hoje), a linha
é o eixo do cano 3,5 cm abaixo do centro (`AdsSightEntry::quality` 2, `drop`).

### A roda de armas (`src/engine/weapon_wheel.h`, `src/ui/modern_wheel.h`)

Segurar o comando `weapon_wheel` (Tab; no controle L3, o único botão que nenhum dos oito estilos usa, `test_input`
confere: o R3 prenderia o analógico direito, que aponta) abre a roda no meio da tela, no estilo Modern (docs/design.md,
"A roda de armas"): no anel de fora as armas que o jogador tem (uma por classe, na variante escolhida, na ordem do ciclo de
`Player_WeaponChange`; o soco aparece como "Unarmed"), no de dentro os aparelhos. Os espaços têm tamanho fixo
(`weapon_wheel_ring_slots`): 8 de 45 graus no anel de fora (12, 16 ou 24 quando o jogador tem mais armas: a trapaça de
todas as armas dá 40 classes, a roda mostra as 24 primeiras) e 6 no de dentro (8, 12 ou 16); as armas ocupam os primeiros a
partir de cima, os outros ficam vazios e apontar um deles não escolhe nada (antes, duas armas eram duas metades da roda).
Cada espaço tem a imagem da arma, o nome do HUD do jogo (`weapon_data` +0x38) e a munição pente-guardada (nos anéis de
24 e de 12 ou 16 aparelhos, só a imagem e a munição; o nome vai para o miolo); o da mão com o losango de ouro; sem munição,
apagado. O mouse (o
movimento vira um vetor de escolha, 260 contagens até a borda; o cursor some e a visão não gira) ou o analógico direito
(o maior empurrão desde que saiu do meio escolhe o anel: todo, o de fora; meio, o de dentro; ao voltar ao meio, o espaço
apontado fica) apontam; o meio não aponta nada. Soltar escolhe pelo caminho da troca do jogo (`weapon_wheel_select`: as
travas e a memória da última arma e do último aparelho de `Player_WeaponChange`, depois `next_weapon`), e a máquina de
estados da arma na mão guarda a arma e saca a nova com as animações do jogo; com a arma ocupada (recarregando), a escolha
espera até 1 s. A roda não abre (e fecha sem escolher) com um menu, numa cena com câmera, com o jogador morto, fora da
primeira pessoa ou desativado; enquanto aberta, `psiInput_SetWheelOpen` tira do jogador girar, olhar, mirar, atirar,
usar, recarregar, os aparelhos, o modo de tiro, a visão e a troca de arma (andar, pular e agachar continuam), e um botão
do mouse apertado com ela aberta fica fora até ser solto.

**A câmera lenta.** Com a roda aberta o jogo anda a `[game] wheel_slowmo` da velocidade (0,25 por padrão; 1 tempo
real, 0 parada; GAMEPLAY > "Weapon wheel slow motion": Pause, 10%, 25%, 50%, Off). O agendador dos ticks do `main.cpp`
(`plan_tick`, `render/frame_interp.h`) continua consumindo os VBlanks no tempo real (o `GameCount` acompanha o
`VBlankCount`), mas só a fração `wheel_slowmo` deles vira tick (cada tick é um tick de 60 Hz): a 0,25, 15 ticks por
segundo. A interpolação desenha entre eles (o registro de cada tick dura os VBlanks consumidos desde o anterior, 4 a
0,25): a 60 Hz, um quarto de tick por quadro, sem degrau (a primeira vez, logo ao abrir, a imagem espera um tick). O som
segue o relógio real, sem mudar de tom: nos VBlanks sem tick o `main` anda a SPU (`audio_out_catch_up`), e os pedidos
do jogo chegam ao driver no próximo `SFXUpdate` (os sons novos, as posições 3D e a música andam no ritmo do jogo). O
tempo da missão (`PlarStat_LogUpdateElapsedTime`, `psiGetTimeIn100ths`) é o do relógio real, como no PS2 (que também
conta em tempo real o modo de 30 quadros por segundo): com a roda aberta ele continua contando (usar a roda custa tempo).

**As imagens.** São as do dossiê do menu principal (`ds_weapons`/`ds_gadgets`, as tabelas copiadas do ELF;
`weapon_wheel_icon` dá a de cada classe por uma tabela do port, porque os nomes do HUD e do dossiê nem sempre são iguais:
"Frinesi Auto 12" e "Frinesi Automatic 12"; as pistolas pelas quatro imagens que `P_DSWEAPONS_Handler` põe por nível de
melhoria, o barbeador pela regra de `P_DSGADGETS_Handler`). Essas texturas só existem no pacote do menu (07000048, item
`0100009a`), que numa fase não está carregado: o `nightfire.exe` as decodifica do FILES.BIN ao iniciar
(`ModernUiApp::load_wheel_icons`: `decode_pack_images`, `data/pack_images.h`, acha cada textura pelo hash no cabeçalho
das paletas, bloco 0x0F; 34 imagens de 128 x 128, ~5 MB lidos uma vez). Cada uma mostra a arma sobre o disco verde do radar
do dossiê; `ui::wheel_icon_image` a recorta (fora do disco, e onde o vermelho e o azul ficam abaixo de ~60% do verde: o
fundo e as letrinhas do radar; o verde-oliva de uma granada passa) e a deixa de um tom só (a luminância sem o verde
refletido, levantada para as armas pretas aparecerem no vidro escuro), e a roda a tinge com a cor do estado (o ouro claro no
apontado). Ficam no renderizador (`Renderer::set_ui_image`, mantidas entre as fases) e são desenhadas pelo Direct3D entre
as duas listas do Direct2D (`ModernUiApp::draw_wheel`, como as imagens do dossiê e do mapa). Sem imagem (o soco, a Kowloon
Type 80, a Delta Repeater, o lança-torpedos, a Q-Pen, o chapéu de Oddjob, a pistola de sinalização: o dossiê não as tem),
o espaço fica com o nome e a munição. O `nf_loader_check` confere a tabela nos dados: `roda de armas: 47 classes com
imagem, nomes batem 47 de 47` (o nome do HUD e o do dossiê dividem uma palavra) `, imagens no 07000048 34 de 34`. As
substitutas HD não valem para essas imagens (elas não passam pela tabela `Tex` do jogo); em 4K a imagem de 128 aparece
aumentada.

**A barra preta** ao lado de "TAB Release to equip" na captura de 07000012 (x 540-650, y 630 em 1280x720) não era da roda:
é o rodapé preto do painel de parede solto do corredor, do cenário (a mesma captura sem a roda o mostra no mesmo lugar),
que caía bem atrás da linha de baixo. A linha agora fica numa pílula escura (`#0b0806` a 72%), e o que a cena tiver atrás
não se confunde com ela.

### Capturas

`--weapon <hex>` (depuração) dá a arma e a pede na mão logo depois da carga; uma lista separada por vírgulas dá todas (a
primeira na mão; só as de modelo carregado na fase entram, como as armas apanhadas) e `all` dá todas as armas e aparelhos da
troca de armas como a trapaça (também as de modelo não carregado: a roda com muitas armas). `--input` aceita `tab`
(segurar a roda) e `mlook=dx,dy` (o vetor de escolha). O relógio do `--input`/`--wait` é o dos ticks: com a roda aberta ele
anda a 1/4. O relatório tem `ads estilo aparelho moderno fixa peso zoom pele linha arma_pos arma_giro alca x y z massa x y
z` (a alça e a massa no espaço da câmera 0: com a pose inteira, 0 0 profundidade) e `roda aberta mostrada armas aparelhos
espacos imagens apontado vetor escolhas ultima aberturas | as armas de cada espaço (* com imagem) / os aparelhos | lento
ticks alpha icones_carregados`.

```bash
# a mira moderna: o Raptor de 07000012, o quadril e a pose (ads ... peso 1.000 zoom 1.350 ... alca -0.000 0.003 0.862)
nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:3" --wait 2.5 --screenshot out/shots/ads/hip12.png
nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:1 rmb:3" --wait 2.5 --screenshot out/shots/ads/ads12.png
# outras armas (a pele precisa estar na fase): a pistola de 07000005, a submetralhadora 0x14, o fuzil 0x16, a espingarda
nightfire.exe --level 07000005 --fixed-video-hz 60 --input "none:1 rmb:3" --wait 2.5 --screenshot out/shots/ads/ads05.png
nightfire.exe --level 07000005 --weapon 14 --fixed-video-hz 60 --input "none:2.5 rmb:3" --wait 4 --screenshot out/shots/ads/w14.png
nightfire.exe --level 07000005 --weapon 16 --fixed-video-hz 60 --input "none:2.5 rmb:3" --wait 4 --screenshot out/shots/ads/w16.png
nightfire.exe --level 07000012 --weapon 1c --fixed-video-hz 60 --input "none:2.5 rmb:3" --wait 4 --screenshot out/shots/ads/w1c.png
# a luneta pelo botão direito (0700000c, o fuzil de precisão: zoom 3,16 desenhado, as faixas pretas dos lados)
nightfire.exe --level 0700000c --fixed-video-hz 60 --input "none:1 rmb:3" --wait 2.5 --screenshot out/shots/ads/scope0c.png
# a roda aberta sobre 07000012 em câmera lenta (o mouse aponta para a direita: um espaço vazio) e uma escolha (o soco, em cima)
nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:1 tab+mlook=400,0:0.4 tab:2" --wait 1.6 --screenshot out/shots/ads/wheel.png
# o Raptor apontado (a 45 graus); cinco armas (o lança-granadas apontado, embaixo à direita); todas (24 + 14 espaços)
nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:1 tab+mlook=400,-400:0.4 tab:2" --wait 1.6 --width 1920 --height 1080 --screenshot out/shots/wheel/w2.png
nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:1 tab+mlook=400,400:0.4 tab:2" --wait 1.6 --weapon 10,1c,2a,34 --width 1920 --height 1080 --screenshot out/shots/wheel/w5.png
nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:1 tab+mlook=-400,-150:0.4 tab:2" --wait 1.6 --weapon all --width 1920 --height 1080 --screenshot out/shots/wheel/wall.png
nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:1 tab+mlook=0,-400:0.4 tab:0.3 none:3" --wait 4 --screenshot out/shots/ads/wheel_pick.png
# a câmera lenta em quadros seguidos: um tick a cada 4 quadros (alpha 0, 0,25, 0,5, 0,75) e o guarda andando 1/4 por quadro
nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:2.5 tab:3" --wait 3.0 --screenshot out/shots/ads/slow.png --frames-dump out/shots/ads/slow --frames 9
```

As capturas sem essas coisas continuam iguais pixel a pixel às de antes (a barra de baixo dos menus Modern muda com o
número do build): sem o botão de mirar, com o controle (o estilo clássico), as cenas e os menus. A luneta mudou de
propósito (o zoom agora aparece). `--bench` no Radeon integrado em 1920x1080 (07000012): com a roda aberta 353 quadros
por segundo fora da tela e 311 numa janela (Direct2D 0,58 ms por quadro, 15 ticks por segundo); fechada, 365 e 327. Com
os espaços fixos e as imagens (11/10/2026, as duas rodas lado a lado na mesma máquina): aberta 343,8 fora da tela e 315,1
numa janela (a anterior: 344,5 e 309,8), com as 38 armas e aparelhos da trapaça 343,1 e 306,3; fechada 376,2 e 331,5.

## O que ainda não entra

- Os morphs do rosto no caminho Direct3D 9 do RTX Remix.
- A cor `SqueezeColourIn` do `CreateSqueezeFrame` (o PS2 clareia a imagem 13% na cópia para a tela; o rastro já
  entra) e o `TColor2` (que a VU1 não usa).
- A máscara de alfa do destino (`ALPHA_1` 0x64 seguido de 0x58, só em 07000041) depende do alfa que
  o céu e os outros desenhos deixam no alvo; ali aparece uma parede cinza que antes não aparecia.
- Texturas animadas mostram só o primeiro quadro.
- Texturas HD em PNG (o pacote atual tem um só) e quadros de texturas animadas além do primeiro.
- Os objetos que ainda são provisórios: só os carros das arenas do multijogador e um da 07000046 (`Car_Create`).
  O brilho dos corpos no infravermelho (`BodyGlow_Create`, um stub) e as bolhas e reflexos do sol (`Bubble_Update`,
  `Sunreflection_Update`, que nenhuma fase cria no um jogador).
- Do som: os sons dos objetos ainda não portados (os `*_Update` de carros e submarinos `Sub_*`/`MiniSub*`: cada um
  toca os seus sons de dentro da sua lógica, que ainda é um stub; torretas, helicópteros, cadeados, objetos de roteiro
  simples, câmeras de segurança, holofotes, o sistema de segurança, o canhão montado, o laser espacial e os corpos
  rígidos já tocam os seus), os bots do multijogador
  (`BOT_soundEffect`, `BOT_handlePain`), a gaussiana da SPU (o port interpola com cúbica) e um teste com fones e caixas
  de verdade (as capturas só medem o WAV).
- Do menu principal: o multijogador (tratadores nulos), o trailer no estilo Modern e as missões de carro (puladas); as câmeras especiais 5 a 10 (a 4 é desenhada nas cenas com câmera; o 2D de todas entra).
- Lógica de jogo e vídeo.
- O RTX Remix: só o protótipo de Direct3D 9 (`--renderer d3d9ff`, não testado com o Remix; ver `docs/remix.md`).
- DLSS/DLAA (a linha está na página de opções, "em breve"), o limite de quadros por segundo (depois da
  câmera desacoplada da lógica) e a tela cheia exclusiva nas capturas (elas desenham fora da tela e não
  mexem na janela; a troca de modo do monitor só foi feita pelo código, sem captura).
