# Estilo de menu Modern: o sistema de desenho

Os menus do PC têm dois estilos (`[ui] menu_style` no `nightfire.ini`, `--menu-style`, e a linha **Menu style**
em INTERFACE nos dois estilos):

- **Modern** (o padrão): um sistema de desenho próprio do port, inspirado nos menus de *007 First Light* (IO
  Interactive, 2026), desenhado com Direct2D e DirectWrite na resolução da janela, sempre (não depende da camada
  2D em alta definição). Este documento é a referência dele.
- **Original**: os menus do jogo como foram portados e a página de opções do PC em abas, com as peças do jogo
  (`engine/menu_pc_options.h`). Sem Direct2D, o Modern não abre e o Original vale.

O Modern cobre a pausa (com as opções numa aba dela e as estatísticas da missão na aba SCORE), o fim de missão, o
resultado, o caminho de um jogador do menu principal (o título, o menu, os codinomes, a dificuldade, o mapa das missões,
o dossiê e os créditos) e o aviso de controle desconectado. As opções juntam as do PC e
as úteis do próprio jogo (as das páginas "Advanced Options" e "AV Options" do menu principal: GAMEPLAY, as legendas,
os volumes); as que no PC não têm efeito (widescreen, ajuste de tela, tela dividida, modo do som) não aparecem no
Modern e continuam nas páginas do jogo no estilo Original. Os menus continuam
sendo os do jogo por baixo: ele os abre, os tratadores dele decidem cada escolha, e as telas Modern só leem o
estado deles e mandam as escolhas pelos eventos dos controles do jogo (`engine/menu_modern.h`; ver `port.md`,
"Menus no estilo Modern"). Ficam no estilo Original as páginas do jogo sem tela Modern: as opções de multijogador do
codinome, o ajuste de tela e o controle das missões de carro.

O código: `src/ui/modern_ui.h` (tokens, tela virtual, movimento, lista de desenho), `src/ui/modern_kit.h`
(componentes), `src/ui/modern_input.h` (a entrada das telas dos menus do jogo), `src/ui/modern_options.h` (a tela
de opções), `src/ui/modern_pause.h` (a pausa), `src/ui/modern_end.h` (o fim de missão, o resultado e o aviso de
controle desconectado), `src/ui/modern_front.h` (o menu principal), `src/ui/modern_dossier.h` (o dossiê, uma vista do
menu principal),
`src/render/ui_modern_draw.h` (o desenho com Direct2D) e `src/render/ui_modern_app.h` (a ligação com o
`nightfire.exe`). Tudo menos o desenho é sem GPU e tem testes (`tests/test_modern_ui.cpp`,
`tests/test_modern_menus.cpp`).

## A linguagem

O que a referência ensina, e o que fica nosso:

- **Fundo**: a cena do jogo continua lá, desfocada e escurecida, com uma vinheta quente. Nada de painel: as
  colunas flutuam sobre o fundo.
- **Linhas finas, nenhuma caixa pesada**: divisórias de 1 px em branco quente a 10%, o sublinhado de ouro
  de 2 px no foco, a faixa fina sob a barra de cima na largura da janela.
- **Ouro e brasa**: o ouro (`accent`) marca o que está ativo (a aba, a categoria, o nome da linha com o foco,
  o preenchimento das barras, a chave ligada); a brasa (`ember`) é o brilho que sobe do sublinhado do foco,
  como uma chama baixa. Nada mais é colorido.
- **Versalete espaçado** para o que organiza (abas, cabeçalhos, seções, títulos), texto normal para o que se
  lê (nomes das linhas, valores, descrições).
- **Três colunas e duas barras**: categorias à esquerda, linhas no meio, a descrição da opção com o foco à
  direita; os comandos embaixo; as abas de contexto em cima.
- **Toques técnicos discretos**: a faixa de dados no alto da descrição, os textinhos dos cantos, os cantinhos
  de baixo. Decorativos, em 35-55% de opacidade, nunca informação.

## Tokens

Os valores padrão estão em C++ (`Tokens` em `src/ui/modern_ui.h`) e o `hd_ui/design/tokens.json` pode trocar
qualquer um sem recompilar (o `nightfire.exe` lê o arquivo ao iniciar; um erro deixa os padrões e aparece na
linha `moderno` do relatório das capturas). Cores em `#rrggbb` ou `#rrggbbaa`.

### Cores

| Token | Valor | Uso |
|-------|-------|-----|
| `scrim` | `#0b0806` 58% | o véu sobre a cena desfocada |
| `vignette` | `#050302` 80% | a borda da vinheta |
| `warm` | `#2a1608` 30% | o calor no meio da vinheta |
| `text` | `#ede6da` | texto principal (branco quente) |
| `text_2` | `#c4bbae` | valores, corpo das descrições, abas inativas |
| `dim` | `#8a8174` | inativo: o cabeçalho "OPTIONS /", textos técnicos, "Coming soon" |
| `disabled` | `#5d564d` | apagado e travado |
| `accent` | `#f2c77a` | ouro: foco, aba ativa, categoria escolhida, preenchimentos |
| `accent_hi` | `#ffe2a8` | ouro claro: o miolo do brilho, o botão da barra com foco, o ponto da opção escolhida |
| `ember` | `#ff8a3d` | brasa: as línguas do brilho |
| `ember_deep` | `#b8400f` | brasa escura: a névoa do brilho, a borda dos degradês |
| `hairline` | `#f5e6d0` 10% | linhas finas |
| `hairline_2` | `#f5e6d0` 18% | linhas finas fortes: o trilho da rolagem, contornos |
| `track` | `#f5e6d0` 13% | trilhos (barra, chave) |
| `knob_off` | `#9a9083` | o botão da chave desligada |
| `keycap` / `keycap_text` | `#d9d2c7` 92% / `#17110c` | as teclas |
| `dialog` | `#120d09` 98% | o fundo do diálogo |
| `meter` | `#f6dba4` | o medidor da descrição |
| `veil` | `#060302` 62% | a faixa funda atrás do texto do fim de missão |
| `ground` | `#0d0a08` | o fundo opaco do resultado e do menu principal (que não têm cena) |

### Tipografia

**Rajdhani** (OFL, `hd_ui/fonts_ofl/rajdhani`: Light 300, Regular 400, Medium 500, SemiBold 600, Bold 700),
carregada numa coleção privada do DirectWrite (nada é instalado no sistema); sem os arquivos, a Bahnschrift.
A faixa de dados usa a Consolas. Nunca itálico.

| Estilo | Tamanho | Peso | Espaço entre letras | Caixa | Uso |
|--------|---------|------|---------------------|-------|-----|
| `tab` | 16 | 600 | 0,20 em | alta | abas da barra de cima, botões do diálogo |
| `header` | 14 | 500 | 0,42 em | alta | "OPTIONS /" |
| `category` | 23 | 500 | 0,03 em | alta | categorias |
| `section` | 14 | 600 | 0,34 em | alta | cabeçalhos de seção ("IMAGE QUALITY") |
| `label` | 23 | 500 | 0,02 em | normal | nome da linha |
| `value` / `option` | 22 | 500 | 0,02 em | normal | valor, opção de lista aberta |
| `title` | 36 | 600 | 0,03 em | alta | título da descrição (encolhe até 24 para caber) |
| `body` | 20 | 400 | 0,01 em | normal | descrição (linha de 1,42) |
| `meta` | 20 | 500 | 0,01 em | normal | "Current value: X", avisos |
| `prompt` | 18 | 500 | 0,02 em | normal | nomes dos comandos de baixo |
| `keycap` | 16 | 700 | 0 | alta | letra da tecla |
| `table_head` | 13 | 600 | 0,30 em | alta | cabeçalhos da tabela de teclas |
| `cell` | 20 | 500 | 0,02 em | normal | células da tabela |
| `footer` / `footer_small` | 17 / 11 | 500 / 700 | 0,02 / 0,22 em | normal / alta | barra de baixo, à esquerda |
| `mono` | 9 | 400 | 0,10 em | alta | faixa de dados, cantos (Consolas) |
| `dialog_title` | 32 | 600 | 0,06 em | alta | título do diálogo |
| `toast` | 18 | 500 | 0,02 em | normal | aviso |
| `menu_big` | 44 | 500 | 0,05 em | alta | as linhas grandes da pausa ("RESUME") |
| `menu_small` | 24 | 500 | 0,08 em | alta | as linhas menores ("OPTIONS", "CONTINUE", REPLAY/QUIT) |
| `menu_sub` | 14 | 600 | 0,24 em | alta | o subtítulo ao lado da linha com o foco ("/ BACK TO THE MISSION") |
| `hero` | 92 | 300 | 0,20 em | alta | "MISSION FAILED" (64 em "MISSION COMPLETE") |
| `hero_sub` | 22 | 500 | 0,30 em | alta | o motivo da falha, o nome da missão no resultado |
| `stat` | 72 | 300 | 0,02 em | normal | os números do resultado |
| `counter` | 96 | 300 | 0 | normal | o contador dos objetivos cumpridos |
| `page_title` | 52 | 300 | 0,12 em | alta | o título das páginas do menu principal ("SELECT CODENAME"; encolhe até 30) |
| `entry` | 60 | 300 | 0 | alta | as letras do codinome digitado |
| `key` | 24 | 600 | 0 | alta | as teclas do teclado da tela |
| `legal` | 14 | 400 | 0,01 em | normal | o texto legal do título (linha de 1,45) |

Tamanhos em pixels da tela virtual (abaixo). Os textos de uma linha ficam com as maiúsculas centradas na
vertical no retângulo deles (a altura das maiúsculas da fonte, `capHeight`), então nomes, valores e teclas
alinham entre si em qualquer tamanho.

### Grade e tela virtual

Tudo é desenhado numa tela virtual de **1920 x 1080**, escalada por igual para a janela: pela altura nas
janelas de 16:9 ou mais largas (21:9 ganha margem dos lados), pela largura nas mais estreitas (4:3 ganha faixas
em cima e embaixo). O fundo (véu, vinheta, a linha fina da barra de cima) vai até as bordas da janela. A
grade é de **8 px**; as medidas principais:

| Medida | Valor | |
|--------|-------|-|
| barra de cima | abas centradas em y 52, linha fina em y 97 | teclas Q/E a 118 da aba da ponta |
| categorias | x 95, a primeira em y 212, passo de 56 | o quadradinho e o cadeado em x 64 |
| linhas | x 552 a 1248, de y 113 a 950 | nome em +18, valor até -26 |
| altura | linha 84, seção 74, opção de lista 76 (recuada 44), tabela 60 (cabeçalho 54) | |
| rolagem | x 1256 | |
| descrição | x 1336 a 1760, faixa de dados em y 138, título em y 212 | medidor de x 1298 a 1866 em y 842 |
| barra de baixo | centro em y 1029, comandos até x 1862, 36 entre eles | teclas de 30 de altura |
| lista grande | x 95, a primeira em y 236, passo de 78 (as menores 54, 30 a mais entre os dois tamanhos) | o quadradinho em x 64 |
| menu principal | o logotipo de 120 de altura em y 120; a lista grande a partir de y 356; as listas das rodas de y 196 a 950 | a imagem da missão de 280 em x 99, y 506 |
| teclado da tela | o nome em y 286 (oito casas de 64, 14 entre elas); as teclas de 60, 10 entre elas, a partir de y 420 | o convite do título em y 908 |
| mapa de botões | duas colunas de 348 no meio, linhas de 58 | o símbolo em +18, o texto em +122 |
| dossiê | as colunas do menu principal: o título e a imagem redonda (280, y 506) à esquerda, a lista de y 196 a 950, a descrição à direita | a medalha dos recordes de 220 x 110 em y 650; as recompensas em quadrados de 92, a cada 122 a partir de y 360 |
| créditos | a tela do PS2 levada à virtual (a altura 448 vira 1080, o meio no meio): as colunas do jogo em x 690 e 1230, as linhas largas em 960 | a área de y 118 a 968, sumindo em 150 nas pontas; o cabeçalho em y 62 |
| aba SCORE | a tabela de x 552 a 1760 (CURRENT e TARGET pela direita, 240 entre elas), linhas de 56 | as linhas recuadas do jogo em +30 |
| fim de missão | o título centrado em y 452; o motivo 86 abaixo; as opções 214 abaixo, 96 entre elas | |
| resultado | o título em y 236, a missão em 340; a pontuação de x 1180 a 1760; as escolhas a partir de y 660 | |

## Componentes

Cada componente desenha numa `DrawList` (retângulos, linhas, textos, elipses, degradês, o brilho, chevrons,
cadeado, losango, glifos de botão, recortes); os que respondem ao mouse guardam os retângulos da última
montagem. Estados: **normal**, **foco** (teclado, controle ou mouse por cima), **ativo** (a aba, a categoria, a
opção escolhida), **apagado** e **travado** (cadeado; tem foco para ler a descrição, não muda nada).

- **Fundo** (`draw_background`): o véu, a vinheta e um escurecido nas faixas de cima e de baixo, na janela
  inteira, com a opacidade do aparecer. O desfoque da cena é do renderizador (`Renderer::blur_background`:
  gaussiana separável em 1/4 da resolução, duas passadas, misturada pela mesma opacidade).
- **Barra de cima** (`TabBar`): abas de contexto centradas em `tab`; a ativa em ouro, as outras em `text_2`; o
  sublinhado de ouro com um brilho baixo desliza entre elas (`tab`, 0,22 s, ease-in-out); as teclas dos lados
  mostram a tecla ligada a "aba anterior/próxima" (Q/E) ou, no controle, LB/RB; textinhos técnicos nos cantos.
- **Categorias** (`CategoryList`): "OPTIONS /" em `header`/`dim`; as categorias em `category`; a escolhida em
  ouro com o quadradinho de 8 px; passar o mouse clareia; travada: cadeado no lugar do quadradinho e texto
  `disabled`; com o foco do teclado na coluna, um sublinhado de ouro curto sob a escolhida.
- **Linha**: o nome à esquerda (`label`), o controle à direita, a linha fina embaixo. Com o foco: o nome vai a
  ouro (cor animada, 0,17 s) e o **foco** (`FocusMark`) fica sob ela: o sublinhado de ouro de 2 px na largura da
  coluna, mais claro no meio, e o brilho de brasa que sobe dele (uma névoa baixa, um halo e onze línguas
  estreitas que tremulam devagar, recortados acima da linha). O foco anda de uma linha para outra (0,17 s,
  ease-out). Com uma lista aberta, as outras linhas recuam a 55%. Perto das pontas cortadas da área, as linhas
  somem aos poucos.
- **Lista que abre** (dropdown): o valor em `text_2` e um chevron fino ⌄; Select (ou o clique) abre a lista no
  lugar, empurrando o resto (0,18 s): as opções recuadas 44 px, cada uma com a linha fina e o ponto de rádio à
  direita (vazio: um círculo escuro com contorno; a escolhida: um ponto de ouro claro com brilho). O chevron
  vira ⌃. Esquerda/direita na lista fechada trocam o valor direto; o valor novo entra subindo 6 px enquanto o
  antigo sai (0,14 s).
- **Chave** (toggle): um trilho de 48 x 24 com um botão quadrado de 14; desligada, botão cinza à esquerda;
  ligada, botão de ouro à direita com brilho (0,12 s); "On"/"Off" à direita. Select alterna; esquerda desliga,
  direita liga.
- **Barra** (slider): trilho de 230 x 4, preenchimento de ouro até o valor, um losango no fim (maior e claro com
  o foco, com brilho), o número à direita. Esquerda/direita andam um passo; o clique no trilho põe o valor e
  arrastar continua.
- **Informação**: só o valor, em `text_2`.
- **Travada**: nome `disabled`, cadeado e "Coming soon" em `dim` (DLSS/DLAA).
- **Ação** ("APPLY CHANGES", "RESTORE DEFAULT BINDINGS"): versalete; apagada sem nada a fazer, ouro quando
  há, com a tecla dela à direita.
- **Cabeçalho de seção**: `section` em `text`, sem linha.
- **Tabela de teclas**: o cabeçalho (ACTION | PRIMARY | SECONDARY | CONTROLLER) em `table_head`/`dim`; as
  linhas com as células centradas; o foco é a célula (o sublinhado e o brilho só sob ela); vazias "—" e o que
  vem do esquema de botões do jogo entre parênteses, os dois em cinza; esperando a tecla, a célula pulsa "…".
- **Indicador de rolagem**: uma linha fina vertical (`hairline_2`) com o pedaço visível em ouro de 3 px.
- **Descrição** (`draw_description`): a faixa de dados (um friso escuro com um traço claro, textinhos `mono` e
  marcas), o título em `title` (encolhe para caber), o texto em `body` (quebrado em palavras), uma linha,
  "Current value: X" (o rótulo em `text_2`, o valor em `text` 600), outra linha, textinhos técnicos e, em ouro
  com um quadradinho, o aviso (mudanças não aplicadas, a tecla esperada, e no ÁUDIO que o som ainda não existe e o
  volume só fica guardado). Nas opções do próprio jogo o texto é o de ajuda do jogo para o valor em vigor (a
  `help_table` do ACTION.ELF: "Feel the action" / "Don't feel the action"), e muda junto com o valor. Embaixo, o medidor opcional: rótulo
  e valor, barra de 18 px (`meter` sobre branco a 20%); nas opções, a resolução da cena 3D (janela x escala, a
  barra é a escala de 0 a 200%).
- **Barra de baixo** (`PromptBar`): à esquerda, dois blocos com a linha fina vertical (o port e o jogo, a versão
  do port); à direita, os comandos (tecla ou glifo + nome), alinhados pela direita, clicáveis; os cantinhos.
- **Tecla e glifo**: a tecla é um quadrado arredondado claro (`keycap`, raio 3, altura 30, largura pelo texto)
  com a letra escura; no controle, o glifo do botão do Xbox (`hd_ui/svg/xbox_*.svg`, os mesmos da camada HD).
- **Diálogo** (`Dialog`): véu de 62% na janela, um painel de 840 x 300 sem caixa pesada (fundo `dialog`, linhas
  de ouro a 45% em cima e embaixo, um traço de ouro no canto), o título em `dialog_title`, o texto, a barra da
  contagem regressiva (encolhe do meio) e os botões lado a lado com o mesmo foco das linhas; aparece subindo
  10 px (0,16 s).
- **Aviso** (`Toast`): uma pílula escura com contorno de ouro e um quadradinho, centrada acima da barra de
  baixo; entra em 0,2 s, fica 2,4 s e sai em 0,35 s.
- **Lista grande** (`BigMenu`): as linhas dos menus do jogo no estilo do menu principal da referência, à
  esquerda: versalete grande (`menu_big`) e uma segunda camada menor (`menu_small`, 30 px abaixo); a linha com o
  foco em ouro (a cor anima) com o quadradinho de 8 px em x 64, o subtítulo `menu_sub` ao lado na mesma linha de
  base ("/  BACK TO THE MISSION") e o sublinhado com o brilho embaixo, do tamanho do nome; passar o mouse clareia;
  as travadas em `disabled` com o cadeado no lugar do quadradinho.
- **Aba travada** (`TabBar::locked`): a aba em `disabled` com o cadeado antes do nome (hoje nenhuma tela usa).
- **Diálogo de espera** (`Dialog::busy`): sem botões, um traço de ouro de 90 px corre e volta devagar na linha
  fina de 360 px (o cartão gravando, o controle desconectado). Um texto de mais de duas linhas aumenta o painel
  por igual para cima e para baixo (`Dialog::grow`).
- **Imagens do jogo** (`ImageCmd`): texturas do jogo (a arte do título, o logotipo, a imagem da missão, as do dossiê)
  desenhadas pelo Direct3D entre duas listas de desenho; o que fica por cima (o véu, o anel escuro em volta da imagem
  redonda) é da lista seguinte. Com `whole`, a textura inteira com meio texel de margem pelo tamanho dela (as do dossiê,
  de tamanhos que a tela não sabe); as substitutas HD valem. Com `port`, uma imagem do port guardada no renderizador
  (`Renderer::set_ui_image`: os ícones da roda de armas, feitos das imagens do dossiê), que `tint` tinge.

## As telas

- **Pausa** (`modern_pause.h`): a barra de cima PAUSE MENU · OBJECTIVES · CONTROLS · SCORE · OPTIONS, como "PAUSE
  MENU · TUTORIALS · OPTIONS" da referência; os textos das abas do jogo onde há (OBJECTIVES, CONTROLS e SCORE vêm dos
  botões da pausa do jogo).
  - PAUSE MENU: "PAUSE /", a lista grande RESUME, RESTART MISSION, QUIT MISSION e, menor, OPTIONS; à direita, o
    painel da descrição com a missão (nome e descrição do jogo), o objetivo atual ("CURRENT OBJECTIVE"), o nível e
    a dificuldade, e o medidor dos objetivos cumpridos. RESTART e QUIT abrem o diálogo com a pergunta do jogo.
  - OBJECTIVES: à esquerda o contador (o número cumprido em `counter` ouro, "/ total" em `dim`, "COMPLETE") e duas
    linhas de fatos (fracassados, confidenciais); no meio as linhas, cada uma com o ícone da situação (cumprido: o
    visto de ouro com brilho; em andamento: o anel com o ponto de ouro; fracassado: o X em `dim`, o texto riscado;
    confidencial: o cadeado, "Classified objective" em `disabled`), o texto do jogo (encolhe para caber) e a
    situação à direita (o rótulo do jogo: "Complete" em ouro, "In Progress", "Incomplete", "Fail"); à direita, o
    objetivo com o foco e a dica do jogo.
  - CONTROLS: à esquerda o aparelho em uso e onde religar as teclas; no meio o esquema (o nome do jogo entre
    chevrons ‹ ›) e o eixo Y (a chave com "Normal"/"Inverted" do jogo), e o mapa de botões do esquema em duas
    colunas: o símbolo do aparelho (o glifo do Xbox ou a tecla ligada) e o que o botão faz.
  - SCORE: como a do jogo, também com um jogador (as estatísticas da missão até agora): à esquerda a missão em
    `hero_sub` ouro, o que a tabela é e a dificuldade; no meio e à direita a tabela do jogo, CATEGORY | CURRENT | TARGET
    em `table_head`, as linhas em `cell` (o valor de agora em `text`, a meta em `text_2`), as recuadas do jogo
    ("Dispatched", "Subdued"...) recuadas com um tracinho, as sem números ("Opponents") como cabeçalho de seção. Só se
    lê: nenhuma linha tem foco. No multijogador, o placar.
  - OPTIONS: a tela de opções, embutida (o fundo e a barra de cima são da pausa).
- **Opções** (`OptionsScreen`): as categorias VIDEO, GRAPHICS, INTERFACE, GAMEPLAY, AUDIO, CONTROLS, MOUSE &
  KEYBOARD e CONTROLLER. GAMEPLAY tem as do jogo em três seções (AIMING: Auto aim, Crosshairs, Manual aim; MOVEMENT:
  Crouch; ITEMS & HUD: Weapon auto switch, Flashing objects, HUD always on), em chaves e nas listas Toggle/Hold;
  INTERFACE ganha ON-SCREEN TEXT com Subtitles (vale na hora, ao contrário das linhas que esperam o APPLY CHANGES);
  AUDIO tem as barras de volume da música e dos efeitos, com o aviso. Deep link: `open_at(categoria)` abre direto
  numa categoria para quem vem de fora das opções (o menu principal abre GAMEPLAY, CONTROLLER e AUDIO pelo menu do
  codinome); voltar das linhas sai da tela em vez de ir às categorias, que continuam à esquerda para o mouse.
- **Fim de missão** (`FailScreen`): sem barra de cima. A cena desfocada com a cortina de sangue do jogo nítida
  por cima, um véu leve na janela e uma faixa funda (`veil`) de y 372 a 790, que some para cima e para baixo; no
  meio, a missão e o nível em `header`, "MISSION FAILED" em `hero` com as linhas finas de ouro que crescem dos lados
  (um losango na ponta), o motivo do jogo em `hero_sub` e as opções do jogo lado a lado em `menu_small`, com o foco
  das linhas e, embaixo, o que a opção faz.
- **Resultado** (`ResultsScreen`): o fundo opaco `ground` com a vinheta e o 007 do jogo (`results_007.svg`) bem
  fraco à direita; a barra de cima RESULTS · STATS · DOSSIER, a aba ativa seguindo a página do jogo.
  RESULTS: "MISSION COMPLETE" (`hero` em 64), o traço de ouro e a linha fina embaixo, o nome da missão em ouro e a
  descrição; à direita a pontuação, a meta da medalha de ouro e a medalha (os números do jogo), com a nota de que a
  pontuação ainda não é registrada; embaixo à esquerda CONTINUE, STATS e DOSSIER (o dossiê, abaixo; voltar dele traz
  o resultado de novo). STATS: a tabela das
  categorias do jogo (CATEGORY | CURRENT | TARGET | TOTAL) e BACK.
- **Menu principal** (`FrontScreen`, `modern_front.h`): o fundo opaco `ground` com a arte do jogo (a do "Press START",
  com o Bond e o logotipo) na altura da janela, as bordas sumindo no fundo. Uma vista por página do jogo:
  - Título: a arte nítida, escurecida embaixo; o convite do jogo ("PRESS THE START BUTTON" em `hero_sub`, com a tecla
    Enter ou o Start do controle e as linhas finas de ouro dos lados), que respira devagar e só aparece depois do
    texto legal do jogo, em `legal` embaixo. Como a tela de abertura da referência.
  - Menu: a arte desfocada e escurecida (o fundo da pausa por cima), o logotipo do jogo em cima à esquerda com o
    traço de ouro, a lista grande NIGHTFIRE · MULTIPLAYER · CODENAMES e, menores, OPTIONS, CREDITS e QUIT GAME, como o
    menu principal da referência; embaixo da lista o texto de ajuda do jogo da linha com o foco.
  - As rodas (o codinome da campanha, os codinomes, a dificuldade, o mapa, o menu do codinome): à esquerda o
    cabeçalho, o título da página do jogo em `page_title` com o traço de ouro e, na campanha, as etapas CODENAME ·
    DIFFICULTY · MISSION (a atual em ouro com o quadradinho); no meio as linhas, uma por item da roda do jogo, com o
    que o item é à direita (Create, Not saved, os losangos da dificuldade, a medalha, o cadeado das missões
    bloqueadas, "Driving", o chevron das páginas); à direita a descrição do jogo. No mapa, a imagem redonda da
    missão embaixo das etapas, trocando com um esmaecer, e a barra de cima MISSIONS · DOSSIER.
  - O teclado da tela: o nome em oito casas (`entry`), o sugerido marcado por um fundo de ouro fraco (a primeira
    tecla o troca), o lugar da próxima letra com o sublinhado de ouro, o brilho e o cursor; a grade das teclas do
    jogo (quadrados de 60 com contorno fino; a com o foco em ouro com o sublinhado e o brilho; SPACE, DELETE e DONE
    numa última linha).
  - As opções: a tela de opções embutida, com a barra de cima MAIN MENU (ou CODENAME) · OPTIONS.
  - O dossiê (`DossierView`, `modern_dossier.h`): uma tela com a barra de cima RECORDS · REWARDS · GADGETS · WEAPONS (os
    nomes do jogo), Q/E (LB/RB) ou o clique trocam. À esquerda "DOSSIER /", o título da página do jogo em `page_title`
    com o traço de ouro, o texto da seção do jogo e a imagem redonda (a da missão nos recordes e nas recompensas, a do
    item nos aparelhos e nas armas) com o anel escuro, como no mapa. No meio a lista: as 12 missões numeradas com a
    melhor medalha (o losango, de ouro do ouro para cima) ou as recompensas ganhas (quatro losangos e "n / 4"); os itens
    com "Upgrade" e um losango por nível de melhoria. À direita, na descrição: os recordes (os nomes das linhas do jogo
    em `section`, os números em `stat` de 52 e 36, a medalha com o 007 do jogo e o nome em ouro, ou "No medal yet"), as
    quatro recompensas (a imagem do jogo num quadrado de 92 com a linha fina, o texto em `label`; as que faltam com o
    cadeado, "Locked" e a medalha que libera), ou o texto do item com a linha da melhoria como o aviso em ouro e o nível
    de melhoria. Enquanto o jogo troca de página (o voltar, a roda, o íris) o conteúdo da aba velha some e o da nova
    entra deslizando; a barra de cima já mostra a aba pedida.
  - Os créditos: sem barra de cima; "NIGHTFIRE / CREDITS" em `header` no canto, as linhas do jogo subindo no meio, no
    ritmo dele, sobre a arte mais escurecida: as empresas em `hero` claro de 34 (versalete, 0,22 em) com um traço de ouro
    curto embaixo, os cargos em `section` de 15 em `text_2`, os nomes e o texto legal em `label`; as duas colunas do
    jogo em x 690 e 1230, as linhas largas no meio; as linhas aparecem embaixo e somem em cima (150 px); no fim, o
    escurecer do jogo.
- **Aviso de controle desconectado** (`WarningScreen`): o diálogo de espera sobre a cena desfocada, o título do port e o
  texto do jogo, com o que fazer no PC.
- **A roda de armas** (`modern_wheel.h`; docs/port.md "Mira moderna e roda de armas"): não é um menu, é uma camada
  sobre o jogo em câmera lenta, enquanto Tab (L3) está seguro; o HUD e a cena continuam embaixo, sem desfoque (o jogo
  segue andando, devagar), só escurecidos: a vinheta da janela (`vignette` a 85%, o meio a 30%) e um disco quente atrás
  da roda (`#0b0806` a 62% no meio, sumindo na borda).
  - A geometria (na tela virtual, centrada em 960 x 540): o miolo de raio 112, o anel dos aparelhos de 122 a 222 e o
    das armas de 232 a 372, setores de anel (`DrawCmd::Sector`) de **tamanho fixo** (`wheel_slot_geometry`): 8 espaços
    de 45 graus no anel de fora (12, 16 ou 24 quando o jogador tem mais armas) e 6 no de dentro (8, 12 ou 16), o primeiro
    em cima e no sentido horário, com uma folga de 3 px de cada lado no meio do anel. As armas ocupam os primeiros, na
    ordem do jogo; os outros ficam vazios. Duas armas não viram mais duas metades da roda.
  - Os espaços: vidro escuro quente (`#18110b` a 80%; sem munição `#0f0b08` a 64%; vazio `#120d09` a 40%) com a linha
    fina da borda de fora (`hairline_2`; `hairline` no vazio e sem munição).
  - O apontado, um acento e não um bloco: o vidro mais claro e quente (`#261b12` a 92%), um degradê de ouro que sobe do
    raio de dentro para a borda (`accent` de 0 a 12%, `DrawCmd::Sector` com `dir` 1), a borda de ouro de 3 px por dentro
    da borda de fora, dois risquinhos de ouro (1,5 px, 18 px; 12 no anel de dentro) descendo pelas bordas radiais a partir
    dos cantos, e o brilho de brasa além da borda (`ember` de 30% a 0 em 30 px, em degradê). O ícone e o nome em
    `accent_hi`, a munição em `accent`. Sem munição: a borda mais clara em cinza (`text_2` a 55%), nada de ouro nem
    brilho. Um espaço vazio apontado: só a borda mais clara (`text_2` a 32%). A arma da mão: o losango de ouro (10 px; 8
    no anel de dentro, na folga entre os anéis) logo além da borda de fora, no meio do espaço.
  - O conteúdo de cada espaço, de pé (não gira com o setor) e centrado no meio do anel: o **ícone** (a imagem do dossiê,
    abaixo), o nome do HUD do jogo em versalete espaçado (17 px, 600, 0,10 em; 14 nos aparelhos), encolhendo até caber
    na largura que a linha tem ali (a corda em cima e embaixo, a espessura do anel dos lados; `fit_style`), e a munição
    como o HUD a mostra ("7-41"; "—" sem munição a gastar) em `value` de 15 (13). Nos anéis em que o espaço tem menos de
    92 de lado (24 armas, 12 ou 16 aparelhos), só o ícone e a munição: o nome vai para o miolo (maior). Sem ícone (o soco,
    as armas que o dossiê não tem), o nome e a munição no meio.
  - Os ícones: as imagens do dossiê do jogo (a arma sobre o disco verde do radar, 128 x 128), recortadas do disco e de
    um tom só (`ui::wheel_icon_image`: o verde do radar fica transparente, o resto vira a luminância levantada), tingidas
    pela cor do estado: `text_2` normal, `text` a da mão, `accent_hi` o apontado, `disabled` (e alfa 55%) sem munição.
    Só a parte do disco (0,1 a 0,9 da textura), num quadrado de 66% do lado do espaço (até 96; 86% nos anéis sem nome).
    Assim as fotos verdes do dossiê viram uma família só, gravada em branco quente e ouro, como o resto do sistema.
  - O miolo: o disco escuro (`#0b0806` a 88%, a linha fina em volta) com o apontado em detalhe: o nome em
    `dialog_title` de 22 (24 quando o espaço não mostra o nome) em ouro, a munição grande em `stat` de 40 e, embaixo em
    `section`, o modo de disparo do jogo, "In hand" ou "No ammo". Sem nada apontado (ou um espaço vazio), o nome da arma
    da mão em `text_2` e "Release to keep" em `dim`.
  - O vetor de escolha: um losango de brasa (14 px, 90%) onde o mouse ou o analógico aponta, com a linha fina de brasa
    (35%) do miolo até ele.
  - Embaixo, 64 px abaixo da roda, numa pílula escura (`#0b0806` a 72%, raio do meio da altura): a tecla da roda (a
    tecla ligada, ou o glifo do controle com ele em uso) e "Release to equip" em `prompt`. A pílula separa a linha do que a
    cena tiver atrás (em 07000012, o rodapé preto de um painel do corredor caía bem ali e parecia parte da roda).
  - O movimento: abrir em 0,12 s e fechar em 0,10 s (tempo real), com a curva de saída cúbica; a roda cresce de 94% a
    100% e o alfa de tudo acompanha. O apontado troca na hora (a mão precisa de resposta imediata).
  - Três camadas: o fundo (vinheta, espaços, realce, miolo, pílula) pelo Direct2D, os ícones pelo Direct3D (imagens do
    port, `ImageCmd::port`) e a frente (textos, losangos, vetor) pelo Direct2D (`ModernUiApp::draw_wheel`).

## Movimento

Rápido, discreto, sempre em tempo real (o relógio do quadro de vídeo, na taxa do monitor, fora dos 60 Hz da
lógica; nas capturas, o relógio dos ticks, para as imagens saírem iguais):

| Movimento | Duração | Curva |
|-----------|---------|-------|
| abrir: o fundo aparece e o desfoque cresce; as três colunas entram em sequência (0,04 s entre elas), deslizando 14 px | 0,25 s | ease-out `(0.16, 1, 0.3, 1)` |
| fechar | 0,18 s | ease-in `(0.5, 0, 0.75, 0)` |
| foco: o sublinhado e o brilho andam; a cor do nome | 0,17 s | ease-out |
| a aba: o sublinhado desliza | 0,22 s | ease-in-out `(0.65, 0, 0.35, 1)` |
| lista abrindo e fechando | 0,18 s | ease-out |
| valor trocado (o novo sobe 6 px, o antigo some) | 0,14 s | ease-out |
| categoria trocada (o conteúdo aparece deslizando 10 px) | 0,20 s | ease-out |
| chave | 0,12 s | ease-out |
| rolagem | 0,20 s | ease-out |
| diálogo | 0,16 s | ease-out |
| setas seguradas | repetem depois de 0,38 s, a cada 0,075 s | |
| fim de missão: o véu e o desfoque | 0,9 s | ease-out |
| fim de missão: o título aparece (em 1 s) e o espaço entre as letras abre de 55% a 100% | 1,8 s, depois de 0,15 s | ease-out |
| fim de missão: as linhas finas crescem; a missão; o motivo | 1,1 s depois de 0,55 s; 0,8 s depois de 0,35 s; 0,8 s depois de 0,7 s | ease-out |
| fim de missão: as opções sobem 12 px (só respondem depois disso) | 0,5 s, depois de 0,8 s | ease-out |
| fim de missão: sair | 0,29 s | ease-in |
| resultado: aparecer | 0,45 s | ease-out |
| menu principal: aparecer (do preto) | 0,6 s | ease-out |
| menu principal: a arte do título desfoca e escurece quando o menu entra | 0,9 s | ease-in-out |
| menu principal: troca de página (o conteúdo aparece deslizando 12 px) | 0,30 s | ease-out |
| menu principal: a imagem da missão troca | 0,32 s | ease-out |
| título: o convite respira (80% a 100%) | 2,6 s por ciclo | seno |
| dossiê: a aba velha some enquanto o jogo troca de página; a nova entra (deslizando 12 px) | 0,20 s; 0,30 s | ease-out |
| créditos: as linhas sobem no ritmo do jogo (2 pixels do PS2 por tick, 4,8 da tela virtual) | o do jogo | |

O fim de missão é o único lento de propósito: dramático, mas contido (nada pisca, nada pula).

As curvas são `cubic-bezier` como no CSS (`curve_eval`); um movimento interrompido continua do ponto em que
estava (`Tween::set`).

## Comandos e símbolos

- Teclado: setas (ou WASD) andam, **Espaço**/**Enter** escolhem, **Esc**/**Backspace** voltam, **Q**/**E**
  trocam a aba de cima, **R** restaura os padrões da categoria, **F** aplica as mudanças pendentes. Os comandos
  do menu da tabela de teclas valem (`menu_up` ... `menu_back`, e os novos `menu_tab_prev`, `menu_tab_next`,
  `menu_defaults`, que só os menus Modern leem e que não tiram botões do controle do PS2).
- Mouse: passar por cima dá o foco, o clique escolhe (abre a lista, alterna a chave, põe o valor da barra,
  troca a categoria ou a aba, religa a célula, aperta um comando de baixo), a roda rola, arrastar mexe na barra,
  o botão direito ou o Mouse4 voltam. O cursor do Windows aparece enquanto o mouse é usado.
- Controle: direcional ou analógico esquerdo, **A** escolhe, **B** volta, **LB**/**RB** abas, **X** padrões,
  **Y** aplica.
- Os símbolos seguem o aparelho em uso (`psiInput_ActiveDevice`): teclas com a tecla ligada (pelo nome curto de
  `input_label`), ou os glifos do Xbox.
- Nos menus do jogo (`modern_input.h`, `NavState`): na pausa, o Start fecha (o do controle e a tecla da pausa,
  Enter por padrão, como no jogo) e Espaço escolhe; voltar no PAUSE MENU fecha também, nas outras abas volta ao
  PAUSE MENU. No fim de missão e no resultado o Start do jogo também escolhe, então Enter escolhe. No diálogo,
  Enter escolhe e voltar responde "não".
- Enquanto a tela está aberta o jogo não recebe nada (`psiInput_SetModal`); ao fechar, o que ainda estiver
  apertado fica de fora até ser solto. A tecla ou o clique que abriu a tela também só contam depois de soltos.

## Fazer e não fazer

- Fazer: deixar o ouro para o que está ativo; uma única cor de destaque por tela; linhas finas para separar;
  versalete espaçado só no que organiza; dizer na descrição o que a opção faz e o valor atual; animar só o que
  muda, curto (até 0,25 s).
- Não fazer: caixas grossas, painéis opacos atrás das colunas, sombras pesadas; itálico; mais de uma cor de
  destaque (o vermelho só no pontinho do canto, e a cortina de sangue do fim de missão, que é do jogo); texto
  pequeno abaixo de 13 px fora dos enfeites técnicos; animações que atrasam a entrada (o foco nunca espera a
  animação; a única exceção são as opções do fim de missão, que esperam o título); informação nos enfeites.
- Novas telas usam os componentes do kit (`src/ui/modern_kit.h`) e os tokens; cores e medidas novas entram nos
  tokens, não no código da tela.
