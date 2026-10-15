# Arquitetura do ACTION.ELF

Mapa do executável das fases a pé, levantado com o Ghidra headless
(`tools/ghidra_headless.sh decomp | callers | callgraph`) e com `tools/layers.py`.
Endereços são da versão NTSC-U (SLUS-20579).

## Boot e laço principal

`main` (`0x00109080`) só prepara o hardware e depois roda o laço de quadro:

1. Reset de DMA, VIF0/VIF1, VU0/VU1 e caminhos do GS; RPC com o IOP; CD-ROM em modo DVD.
2. `FS_Init`, heap do IOP, interrupções de VIF1 (`0x123e90`) e VBlank (`0x124088`).
3. `Mem_Init`, idioma (`PS2FreshBoot`), memory card, `CreateDB` (double buffer do GS).
4. Dispara as cadeias DMA dos microprogramas para VIF1 (`0x241c80`) e VIF0 (`0x244280`).
5. Timers, `system_input_init` (controles e multitap), `PS2RegionChange`.
6. Laço infinito:
   - `PS2FramesToSkip = VBlankCount - GameCount`, limitado a 1 ou 2.
   - Se lógica + desenho passam de 262,5 linhas (meio quadro NTSC), força 2 por 10 quadros.
   - `GS_SetRefreshRate(PS2FrameRate / skip, PS2FrameRate)` e `GameFlow_Main()`.
   - Com vídeo de fundo tocando, decodifica um quadro do MPEG antes.

O jogo roda a 60 fps e cai para 30 sob carga. Não há tempo de quadro em segundos:
a lógica lê variáveis globais preenchidas por `GS_SetRefreshRate` (`0x0017d2b8`):

| Variável | Valor |
|----------|-------|
| `VIDEO_FRAME_RATE` | taxa do vídeo (60 no NTSC) |
| `FRAME_RATE_INT`, `FRAME_RATE` | taxa atual do jogo (60 ou 30) |
| `FRAME_RATE_DIV` | `FRAME_RATE / 60` |
| `FRAME_RATE_MUL` | `60 / FRAME_RATE` |
| `REC_FRAME_RATE` | `1 / FRAME_RATE` |

São 237 funções que usam essas variáveis (85 delas na IA `NDrone2`, 21 no jogador).
A lógica já foi escrita para escalar com a taxa, o que viabiliza 120 ou 144 fps no PC.
Risco conhecido: `FRAME_RATE_INT` aparece em divisões inteiras como
`VIDEO_FRAME_RATE / FRAME_RATE_INT`, que só fecham para divisores de 60. Cada uso precisa
ser revisto ao subir a taxa; a alternativa segura é lógica a 60 Hz com interpolação no desenho.
É o que o port faz (`docs/port.md`, "Interpolação entre ticks"): a lógica fica a 60 Hz (ou 30, como o PS2 sob
carga), o `Game_Draw` roda uma vez por tick, como no `main` do PS2, e o renderizador desenha os quadros de vídeo
entre os registros dos dois últimos ticks.

## GameFlow: pilha de estados

`GameFlow_Main` (`0x001c98c8`) roda uma vez por quadro e olha o topo de `GameStateStack`
(até 64 níveis). `GameState[0x40]` pula o desenho nos estados de carga.

| Estado | O que faz | Sai quando |
|--------|-----------|-----------|
| 1 | Boot: `Mem_Init`, `bootup_bootup`, `Boot_LoadPTPData`, `Reset_MapLoadSettings`; empilha 2 | imediato |
| 2 | Com `GameState.start_request` (+0x18), empilha 3; sem ele, o jogo roda: é o estado de jogo | pedido de fase |
| 3 | `ResetMap_Load`: carrega a fase | a carga troca o estado |
| 4 | `Locks_Init` | imediato (desempilha) |
| 6 | Vídeo em tela cheia | vídeo acaba ou botão |
| 8 | Espera o esmaecimento da câmera 6 que o pedido 7 começou | `fade_state` deixa de ser 1 |
| 9 | Troca para o `DRIVING.ELF` (`psiLaunchDriving`) | não volta: reinicia pelo `BASE.ELF` |
| 10 | Só `Input_Update` e `Game_Draw` (menu sobre a cena congelada) | `CheatInfo + 0x10` ligado |
| 0xC | Contagem regressiva em `viewer_tag::fade_speed` da câmera 6 (+0x22C), gravada pelo pedido 0xB | chega a zero |
| 0xE | Só câmeras (`Camera_UpdateAll`, `SkipCodeFrame`), pulando a lógica, durante o esmaecimento da câmera 6 que o pedido 0xD começou | o esmaecimento acaba |
| outros | Jogo normal | |

No fim de cada quadro: `Game_Run()` (salvo se `SkipCodeFrame`) e `Game_Draw()`.

Portado em `src/engine/gameflow.cpp`. O boot deixa a pilha em `[1, 2]`: o estado 1 fica embaixo, e
o 2 (`GF_WAIT_START`) é onde o jogo roda depois de cada carga. `Reset_MapLoadSettings` pede a fase
do `GameState` ou, sem ela, o menu principal (`07000048`). Quem troca de fase é
`ResetMap_LevelToLoad`, que só aceita o pedido nos estados 2, 7, 8, 13 e 14. As fases da campanha
passam antes pelo filme de abertura (códigos `071xxxxx`, a não ser com `no_movie`), e as do
multijogador ligam `GameState.multiplayer`. A cada tick `GameFlow_Main` também conta `ticks`,
`frame` (fora de pausa) e `video_frames`, e calcula `pulse`, uma onda triangular de 0 a 62 usada
para piscar textos.

A câmera 6 é a do esmaecimento entre fases: os pedidos 7 e 0xD só empilham o estado se `Camera_SetFade(6, tempo,
cor, 1)` começar (o tempo em ticks é o argumento do pedido), e o 0xB grava a contagem nela. Antes, esses pedidos
passavam por ganchos que o `main` não ligava: o estado era empilhado sem esmaecimento e o 0xB não gravava a contagem;
agora o preto aparece (ver "Esmaecimentos, rastro e tela de carga").

### Troca com o DRIVING.ELF

`psiLaunchDriving` (`0x00120af0`) desliga as interrupções, copia o bloco PTP para o endereço
fixo `0x100000`, liga o flag em `0x10097c` e reinicia por `cdrom0:\BASE.ELF`, que carrega o
outro executável. Na volta, `Boot_LoadPTPData` (`0x001c8560`) lê o bloco de 0xA50 bytes
(`psiGetDrivingData`), interpreta-o com `LS_LoadFromBuffer` (mesmo formato do save) e restaura
missão, pontuação, idioma, cheats, desbloqueios e configurações de multiplayer.
No PC as duas metades podem trocar esse mesmo bloco em memória, sem reiniciar o processo. O estudo do outro lado (o
`BASE.ELF`, o `DRIVING.ELF`, cada campo do bloco e os resultados que voltam) está em `docs/driving.md`, seção 4.

## Memória

O módulo `Mem` (0x001cf568-0x001d009c, portado em `src/engine/memory.cpp`) gerencia um único heap
contíguo. No PS2, `psiMem_Init` pega o maior `malloc` que termina abaixo de 0x1fe0000; o `main`
chama `Mem_Init` no boot e `ResetMap_Load` o chama de novo a cada fase, então o heap inteiro é
descartado na troca de fase.

Cada bloco tem um cabeçalho de 12 bytes: endereço do anterior (32 bits; o primeiro aponta para si
mesmo), tamanho com cabeçalho, etiqueta de 16 bits, folga do fim e marca de livre. A etiqueta leva
a categoria no byte alto (`Mem_Info` soma o uso por categoria) e o alinhamento padrão no byte
baixo; a etiqueta `0x2980` arredonda o tamanho para setores de 0x800. `Mem_Init` preenche o heap
com 0x98.

| Modo (`Mem_SetMallocMethod`) | Busca | Onde aloca no bloco |
|---|---|---|
| 0 | a partir do `QuickBlock`, para trás e depois para frente | no fim |
| 1 (padrão) | o menor bloco livre que serve | no fim |
| 2 | o primeiro a partir do início | no começo |
| 3 | o de endereço mais alto | no fim |

`LoaderLoad`, `ResetMap_Load`, `LoadableLoad` e `psiCreateMapTextures` trocam o modo durante a
carga. `Mem_Free` junta o bloco com os vizinhos livres, exceto o último bloco do heap, que não se
junta ao anterior (peculiaridade mantida). `Mem_Shrink(&p, n)` libera os `n` bytes a partir de `p`
dentro de um bloco em uso (começo, meio ou fim); `CheckSizeAndShrink` o usa para devolver a sobra
de um buffer. `Mem_ShrinkToEndOfBlock` corta o bloco em `p`.

No PC o heap é uma reserva de `VirtualAlloc`. Na versão de 32 bits os endereços de 32 bits dos
cabeçalhos e os ponteiros dentro dos dados carregados funcionam como no PS2; na de 64 bits o heap sai
da arena, dentro da janela de 4 GB a partir da imagem, e esses campos guardam o deslocamento a partir
dela (`P32`; ver docs/port.md, "64 bits").

## Carregador de fases

Portado em `src/engine/loader.cpp` e `src/engine/parsemap.cpp`; estruturas em
`src/engine/map_types.h`.

`ResetMap_Load` reinicia o heap e a tabela de hashcodes, cria o mundo (`build_alloc_world`) e chama
`LoaderLoad` até devolver 0, duas vezes: com `LOADER_ANIMS` (o pacote `07F…`, animações) e com
`LOADER_LEVEL` (o pacote `070…`). `LoaderLoad` é uma máquina de estados:

| Passo | O que faz |
|---|---|
| 0 | monta o nome (a seção vai nos bits 20-23 do hashcode) e abre com `psiFileOpen` |
| 1 | lê o setor de cabeçalho e o diretório |
| 2 | soma o tamanho dos arquivos do lote (todos, porque `MaxFilesToLoad` é -1) |
| 3 | lê o lote inteiro num bloco do heap (na fase principal, com o modo de alocação 2) |
| 4 | entrega um arquivo por chamada ao `LoaderProcess`, que despacha pelo tipo |
| 5 | fecha e libera cabeçalho e diretório |

Os itens de mapa (tipos 0, 1, 2 e 0xb) passam por `parsemap_parsemap` em quatro chamadas: abrir,
percorrer os blocos (`parsemap_handle_block_id`), criar os objetos dinâmicos (itens que não são a
fase principal) e descartar o fim do arquivo a partir do bloco 0x1c (`LoaderShrink`). A fase
principal faz os objetos dinâmicos e o descarte no próprio `ResetMap_Load`, depois de ligar as
células às salas.

O arquivo continua carregado: as estruturas apontam para dentro dele.

- `celglist_tag` (0x50 bytes, uma por entidade): parâmetros, dicionários de geometria (`entgfx_tag`),
  colisão, esfera e caixa, LOD.
- `cel_tag` (0xb0 bytes, uma por posicionamento estático com o bit 0x8000): posição, rotação, esfera
  no mundo e lista de portais, numa lista ligada a partir de `glb_world`.
- `portal_tag` (0xa0 bytes): cada registro do bloco 0x21 vira dois, um em cada célula.
- `map_tag`: tabelas de texturas e paletas; cada textura vai para a tabela de hashcodes.

O `nf_loader_check` carrega os 36 pacotes pelo `ResetMap_Load` e confere células, portais e
objetos dinâmicos com uma contagem independente feita pelo leitor de `src/data`.

### Objetos

Cada objeto (`obj_tag`, `src/engine/object.h`) tem um cabeçalho de 0x110 bytes (posição, rotação,
esfera no mundo, matriz, modelo, flags, cor da luz da sala) seguido dos dados do tipo. Os objetos
ficam em `DynamicObjList` e, ao mesmo tempo, na lista da sala onde estão (`cel_tag::objects`).
`build_LinkToRoom` acha a sala pelo centro da esfera (`build_FindCel`: as salas são as células com
o bit 0x40000 no tipo; com mais de uma, raios para cima e para baixo decidem) e copia a cor da luz
da sala para o objeto. Objetos que atravessam salas ou que não têm sala vão para a `ForcedList`.

O byte `type` (+0xff) indexa `control_funcs`, a tabela de {atualização, colisão, remoção} de cada
tipo (85 tipos). Não há função de desenho por tipo: o módulo View desenha os objetos pelo modelo
(+0xd8) e pela matriz (+0x90).

`parsemap_create_dynamic_objects` escolhe o `*_Create` pelo tipo exato do posicionamento (as flags
inteiras, sem máscara). Quase todos só criam algo na fase principal. A matriz dos objetos vem de
`View_RotTransMatrix`, que usa uma ordem de rotação diferente da `RotMatrix` das células (iguais só
com rotação em y), e a escala do posicionamento não é usada. Os tipos mais comuns nas 35 fases:
Drone (inimigos), nós de cobertura da IA, Pickup, Break (quebráveis), Door e gatilhos.

### Colisão

Portada em `src/engine/collide.cpp`. Um teste é uma esfera (0x100/0x101), um raio (0x201), uma
cápsula (0x800) ou uma cápsula que só junta triângulos (0x2000) (`HITTEST_tag`). A fase larga
(`Collide_Pick`) parte da sala do teste, segue os portais que o volume cruza (`Collide_StraddleCels`,
uma visita por portal a cada busca) e junta candidatos: as células dentro de cada sala, os objetos
(`Collide_PickObj`: filtros da máscara do teste e esfera do objeto) e a própria sala. A fase fina
(`Collide_Intersect`) leva o teste para o espaço local da célula ou do objeto (a matriz dele) e
percorre a árvore de caixas da colisão até os triângulos.

A cápsula (`Intersect_CylGeom`, escrita à mão para a VU0) procura, para cada triângulo perto da caixa
do segmento, o ponto do segmento mais próximo dele (onde o segmento cruza o plano ou, quase paralelo,
o mais próximo do centroide); se a distância ao triângulo é menor que o raio, empurra o segmento para
fora e marca `found` bit 0 quando o contato é um piso para o eixo da cápsula. Objetos articulados sem
colisão própria (bit 8 de `mode`, os personagens) passam por `Collide_Jointy`: cápsula contra a
cápsula do outro objeto (o testador sai todo o caminho no jogo de um jogador, metade no
multijogador); raios e esferas contra as caixas das juntas de `AnimGetCollData` (portada com o corpo da IA,
`drone_move.cpp`). O teste 0x2000 (`Intersect_CylTriGeom`) só copia para `TriHeap`, no mundo, os triângulos
a menos do raio, para `Collide_PlaceObject` assentar objetos no chão.

Os dois testes que faltavam (o módulo Collide está completo): `Collide_CylinderIntersect` é a cápsula de um teste já
montado (início, fim e raio de quem chama; o tipo vira 0x800) contra a geometria, com um testador temporário e os
acertos ordenados como em `Collide_SphereIntersect`; só os pêndulos dos corpos rígidos a usam
(`RB_RestingContactRot`). `Collide_PlaceObject` (de `GT_DeployMiniGun`, a metralhadora 0x52 montada) sobe o ponto
5 cm, acha o chão num raio de 3 m para baixo do "cima" da matriz, põe o ponto no chão + normal x raio, junta com uma
cápsula 0x2000 de 10 cm os triângulos em volta (até 0xa6) e recusa se algum plano passa a menos de raio - 0,13 do
ponto; os quase paralelos ao chão (0,96) até 0,2 além do raio formam a caixa do chão, que precisa ir pelo menos a
meia largura (|lado| + |frente| da matriz alinhada ao chão) nos dois cantos. O port devolve ao teste o testador de
antes no fim de `Collide_CylinderIntersect` (o original o deixa apontando para a pilha).

A colisão de cada entidade (blocos 0x2e/0x2f) tem uma árvore de caixas de 0x30 bytes; as folhas
guardam um intervalo de triângulos e um bloco de vértices quantizados (s16 vezes a escala da folha
mais o deslocamento). Cada triângulo tem três índices de vértice e o índice da normal (três s16
divididos por 16383, ou 16384 no teste de raio, e o d do plano em float), e um byte de material.

As salas são as células dos tipos 0xc023 e 0xc047. `build_link_objects_to_rooms` pendura cada
célula estática dos tipos 0x8017-0x8019 na sala cuja geometria está mais perto do centro dela; o
módulo de visão só desenha as células penduradas nas salas visíveis.

### Texturas e geometria na carga

`psiCreateMapTextures` (uma vez por item, no primeiro bloco de parâmetros) cria um registro de
0x20 bytes por textura e procura o CRC-32 dos pixels na tabela global `Tex` (1024 entradas de 0x20
bytes; texturas animadas ocupam uma por quadro). Uma textura igual de outro item (mesmo CRC e
mesmos bytes de pixels e paleta) reaproveita a entrada. Pixels e paleta são copiados para o heap,
porque o original fica na parte descartada do arquivo, e `bmp[i].data` passa a apontar para o
registro. Efeito colateral mantido: o modo de alocação termina em 3 (o bloco mais alto).

`psiCreateEntityGfx` troca os deslocamentos do bloco 0x2d por ponteiros: cabeçalho, caixas
(0x38 bytes), cadeias de DMA, listas de texturas e os ponteiros do skinning. A referência de
textura dentro da cadeia passa a apontar para o pacote de registradores do GS da entrada de `Tex`,
e a lista de texturas de cada caixa vira a lista de índices de `Tex` sem repetição.

## Um quadro

`Game_Run` (`0x001c9790`, portado em `src/engine/game.cpp`), na ordem:

1. `psiPreGame_Run`, `Input_Update`
2. Se o jogo não está congelado: `Sound_UpdateListeners`, `MenuManager_Update`,
   `MenuManager_Monitor`, `Mission_Update`, `MP_Update`, texto da tela, `UpdateAllShards`,
   `Env_Update` (o vento das salas e as gotas: chuva, neve, bolhas; ver "Chuva, neve, raios e nuvens")
3. Fora de pausa: `SSys_Monitor`, `Light_Update`, `control_movement_object_handler(0)`
   (atualiza todos os objetos: jogador, IA, portas, projéteis...)
4. `Camera_UpdateAll`, `psiPostGame_Run`

`control_movement_object_handler` (`0x001336f8`, portado em `src/engine/control.cpp`):

1. `Doors_Calc` e `Trigger_Calc`. Salas do tipo `0xc047` com canal de switch trocam para o modelo
   alternativo (`celglist_tag::lod`) quando o canal muda. `switch_channels_hold` guarda os canais
   do tick anterior e `switch_channels_prev` os de dois ticks atrás (para detectar quando um canal
   acabou de ligar).
2. A atualização de cada objeto (`control_funcs[type].update`).
3. A remoção dos objetos marcados (`life & 1`), repetida até não sobrar nenhum. Com `mode & 0x400`,
   as referências a eles em outros objetos são apagadas.
4. A acomodação dos que se mexeram (`mode & 0x20`): ângulos de volta a (-pi, pi), esfera,
   `control_handle_cel_change` (troca de sala pelos portais) e a luz ambiente
   (`Lights_CalcAmbientLight`).
5. `Collide_Update`: devolve os acertos do tick anterior e roda o teste de cada objeto (fase larga
   e fina). Os acertos são ordenados pela distância, cada acerto contra outro objeto é copiado para
   a lista dele, e por fim roda a função de colisão de cada tipo.
6. A acomodação de novo; por fim, objetos presos a um dono (`owner`) seguem a sala dele.

`Game_Draw` (`0x001e6f50`) percorre até 4 visões (tela dividida do multiplayer) em passes:

1. `View_CaptureScene` de cada visão; névoa
2. Céu de fundo (`View_DrawSky(v, 1)`)
3. Lista 0: objetos opacos (`View_DrawObjList(v, 0)`), depois `psiPostSolid`
4. Céu da frente (`View_DrawSky(v, 0)`)
5. Lista 1: transparentes; estilhaços (`DrawAllShards`) e gotas (`DrawDrops`)
6. Limpa o Z se preciso; lista 2: sobreposições (a arma e os braços em primeira pessoa, o clarão do tiro). Com algo
   nas listas 2 (`Tots[2]`), a câmera 5 entra em uso e `psiClearZ` manda o `ZClearPacket` (`CreateZClearSprite`: um
   SPRITE da tela inteira que só escreve Z = 1, o mais longe para o GEQUAL do GS); a lista 2 das câmeras 0-3 vem depois,
   sempre por cima do cenário e testada só contra ela mesma. As câmeras 4-9 desenham a lista 2 delas sem limpar. O port
   marca o ponto no registro do quadro (`PsiFrame::clear_z_at`) e o renderizador limpa a profundidade ali (`docs/port.md`,
   "A arma em primeira pessoa e a limpeza do Z")
7. Sprites, textos e HUD (`Sprite_BuildList`, `View_DrawSprites`; ver "Camada 2D"), e o esmaecimento da visão
   (`psiFadeView`, ver "Esmaecimentos, rastro e tela de carga")
8. As mesmas etapas para as visões de 4 em diante (menus e janelas especiais)

O `Game_Draw` também guarda estado de um quadro para o outro: o `GlobalBlur` (o rastro da tela, `psiSetScreenBlur`)
cai 16 por chamada, o
`GameState.draws` conta as chamadas, o `View_DrawSky` conta a espera dos relâmpagos e do trovão (e sorteia com o
`Rand`) e gira o céu da 0700001b um passo por chamada, as células e os objetos com a flag 4 tremem com o `Rand`.
No PS2 ele roda uma vez por quadro do jogo; no port também (uma vez por tick). Só do port, sem mudar o desenho:
o `View_DrawObjList`, o `View_DrawSky` e os anexos dizem ao `psi_draw` quem pede cada desenho
(`psi_draw_set_owner`: a sala, a célula, o objeto, o elemento do céu ou a marca, a lista e a ordem dentro do dono),
a identidade com que a interpolação acha o mesmo desenho no tick seguinte.

### Câmeras e visibilidade (portado, `src/engine/view.cpp`)

Cada câmera é um `viewer_tag` (0x240 bytes) em `glb_viewer[0..10]`. `Camera_CreateCameras` cria as
câmeras dos jogadores (0-3, só a 0 fora do multijogador) e as especiais (4-10; a 5 e a 7 têm um
mundo próprio); quem liga a câmera do jogador é o `Player_Init`. A matriz `camera` (+0x120) tem a
linha 2 para a frente e a linha 3 na posição; `Camera_Update` deriva dela a matriz de visão, a
posição e a de billboards (a linha 0 negada). `Camera_CheckLocation` acompanha a sala da câmera:
cruzou um portal desde o quadro anterior (`Cel_ObjectLeftCel`), passa para a sala do outro lado;
saiu da caixa da sala sem cruzar portal, procura de novo com `build_FindCel`.

`View_CaptureScene` monta as listas do quadro:

1. `Vision_Calculate_View_Fustrum`: pirâmide da câmera até 200 unidades (a base usa o campo de
   visão vertical e a proporção), cinco planos na profundidade 0 e os quatro lados copiados para a
   câmera.
2. `Vision_Recurse`, a partir da sala da câmera: cada portal visto de frente (a câmera do lado
   negativo do plano dele), com `flag` ligada e sem porta fechada (`Door_IsClosed`), é recortado
   pelos planos da profundidade atual (`ClipPortalWithPlane`). Sobrando algum pedaço, a sala do
   outro lado entra em `VisCelList` (até 64 salas); com três vértices ou mais, os planos que vão da
   câmera às arestas do pedaço, mais o plano do portal, viram o frustum da profundidade seguinte.
   Até 46 níveis e 100 chamadas por quadro.
3. As salas visíveis são ordenadas pela profundidade e ligadas por `cel_tag::vis_next` a partir de
   `GfxList + 4`. Se alguma tem o bit 28 do tipo apagado, o céu fica visível.
4. `View_AddCels`: cada sala visível vai para a lista 0 (chaves -90000, -89990... na ordem dos
   portais); as células penduradas nela (`children`) e os objetos dela entram se a esfera estiver no
   frustum (`Vision_InView`), com a distância² à câmera como chave: lista 1 (de trás para a frente)
   com a flag 1, lista 2 com a flag 0x2000, senão lista 0. `View_AddForcedObjects` acrescenta os
   objetos que atravessam salas (flag 0x8000).

`View_DrawObjList` ordena cada lista pela chave e desenha: salas e células com
`RotMatrixZYX(rot)` + `pos` (ou a rotação da câmera, para billboards), objetos com a própria matriz,
e os anexos de cada um (`attach`, +0x10). `View_DrawSky` desenha os elementos do céu da camada pedida
na posição deles ou, com a flag 2 do modelo, na da câmera; elementos com modelo de relâmpago trocam
de modelo a intervalos aleatórios e tocam o trovão depois (`Sound_Play` 0x5d/0x5e).

### Portas (portado, `src/engine/door.cpp`)

Uma porta é um objeto do tipo 0x1c com 0x80 bytes de dados (`DOORINFO`), criado por `Door_Create`
a partir dos parâmetros do posicionamento: grupo, canal de switch, canal de tranca, sons, sentido
único e se abre ao toque. Ela não gira em torno de uma dobradiça calculada: segue o próprio caminho
(bloco 0x19, um quadro de 0x1c bytes por ponto: posição e quatérnio), e a posição no caminho
(`pos`, de 0 a 1) vira a matriz por `Spline_Interp` (Catmull-Rom na posição, slerp na rotação).

- De girar (`unknown_f6` 0): fechada no meio do caminho (0,5); abre para 0 ou 1, para longe de quem
  empurra (`hinge` é a inversa da matriz fechada, usada para saber de que lado a pessoa está), e não
  fecha se houver alguém no caminho da folha.
- De correr (1 e 2): 0 fechada, 1 aberta. A do tipo 1 conta sempre como fechada para a visão.
- Estado em `obj_tag::state`: 2 fechada, 4 aberta, 8 abrindo, 0x10 fechando. O canal da porta fica
  ligado enquanto ela está aberta, e ligá-lo de fora abre a porta; o canal de tranca a destranca.
  Aberta, fecha sozinha depois de 6 segundos se ninguém estiver perto.
- As portas de um grupo copiam o movimento da primeira que se mexeu no quadro (`Doors_Calc`).
- `build_LinkDoors2Portals` guarda cada porta marcada no portal que ela cobre (e no oposto); com a
  porta fechada (`Door_IsClosed`), `Vision_Recurse` não passa por aquele portal.

### Animação (portado, `src/engine/anim.cpp`)

Os personagens e objetos animados usam o esqueleto do pacote 07F (`pSkeletons`) e três tipos de
arquivo carregados por `AnimLoadFile` durante a carga da fase: peles (05, os modelos com skinning, os
pais dos ossos, a pose inversa e os anexos presos aos ossos), sequências (04, os canais de cada osso
como polinômios por trechos de quadros) e scripts (06, quantos quadros, o que fazer no fim e
comandos por quadro: tocar uma sequência num intervalo, um som, um evento como um passo).

Um objeto animado tem uma lista de scripts tocando. A cada tick (`AnimObjectUpdate`):

- cada script anda `speed` quadros (1 por padrão: as sequências tocam a 60 quadros por segundo),
  ou o quanto o objeto andou (sincronia por distância, para os pés acompanharem o chão), ou a fase
  de outro script; no fim para, repete ou vai e volta, e executa os comandos dos quadros que passou;
- as sequências dos scripts decodificam o quadro (interpolando entre dois) em translação e
  quatérnio por osso, e medem quanto a raiz andou;
- `AnimFrameResolve` mistura os scripts pelo peso de cada um (entram e saem em alguns ticks), anda o
  objeto o quanto a raiz andou e dá a altura do quadril (`root_height`, que a cápsula do jogador
  usa);
- `psiBuildMatrixPalette` encadeia os ossos (quatérnio no mundo = pai ⊗ local) e monta a paleta do
  skinning, que o desenho (`AnimObjectDraw` → `psiDrawSkinObjectMatrix`) usa na GPU.

Os "conjuntos" (`AnimSet*`) escolhem e misturam scripts pela velocidade (parado, andando devagar,
rápido) e, no multijogador, o passo lateral. No modo história o corpo do Bond usa sempre a postura
06000a00 e as transições de agachar; ele só aparece na câmera livre do port. O rosto (morphs) e o olhar
da cabeça estão em "Rostos", abaixo, e a mira do tronco em "Mira do tronco". Os scripts de cena
(`Script_Load`) são outro módulo: ver "Script player" abaixo.

### Rostos (portado, `src/engine/anim.cpp`; o desenho em `src/platform/psi_draw.cpp` e `src/render`)

As cabeças dos personagens têm 22 alvos de morph (bloco 0x2B; formato e alvos em docs/formats.md, "Morphs do
rosto"). O caminho dos pesos, por tick e por desenho:

1. **Os scripts de rosto.** Toda pele com morphs aloca o slot 1 (rosto) em cada script. As sequências de rosto (22
   canais, um peso cada) decodificam ali, só de perto (closeness < 13, sempre numa cena). Um script cuja primeira
   sequência é de rosto fica fora da mistura do corpo (flag 0x40000). As marcas: 0xe0000000 expressão parada,
   0xe0000001 piscada, 0xe0000002 fala.
2. **`AnimFacialUpdate`** (`AnimObjectUpdate`, nos personagens com 71 ossos ou mais, de perto, fora de cena e sem
   a flag 0x10000): o estado em `ANIMOBJ_tag::face_state` (+0x109).
   - 0: começa a expressão do alerta 0 (`Male/Female_Facial_State_Anims[0]`, velocidade 0, fim 3) e marca a
     primeira piscada para daqui a 1,5 a 4,5 s (`face_timer`, +0x102);
   - 1, parado: se o estado de alerta do personagem mudou (`Drone_AlertStatus`, `Drone_tag` + 0x51c: alerta,
     anterior, mudou), apaga os scripts só de rosto e começa a expressão do novo alerta (tabela pelo alerta 0 a 3);
     senão, fora do alerta 3, conta até a piscada (o índice 4 da tabela, fim 3) e sorteia a próxima: 1 a 3 s, ou 2 a
     4 s no alerta 0. Só nas mulheres (esqueleto 1; os homens têm `anim_flags` 0x400): metade das vezes nada mais, um
     quarto pisca de novo logo (0,1 a 0,2 s; o bit 0x80 do estado marca e a piscada seguinte apaga) e um quarto
     pisca devagar (velocidade 0,2 a 0,4);
   - 2, falando (posto por `AnimFacialAppend`, de `NDrone2_SpeechSFX`): quando não sobra script de fala, volta a 1
     e marca o alerta como mudado (a expressão é refeita no tick seguinte).
   Numa cena (`ScriptCam`) ele não faz nada: todo script novo de uma pele com morphs nasce marcado 0xe0000002
   (`AnimScriptInit`), e o rosto é o que a cena toca.
3. **`AnimFacialBlend`** (no fim de `AnimFrameResolve`, de perto): `ANIMOBJ_tag::morph_weights` (+0xb0) a partir do
   quadro de rosto do primeiro script de cada marca. Fora de cena: a expressão inteira, por cima a piscada e a fala
   onde o peso passa de 0,01 em módulo, e por fim os olhos: com `eye_x`/`eye_y` (+0x107/+0x108, 0 a 255) o alvo 14
   recebe `eye_x / 255`, o 13 `1 - eye_x / 255`, o 11 `eye_y / 255` e o 12 `1 - eye_y / 255`, cada um só se passar
   de 0,01. Numa cena, só a fala, inteira.
4. **`AnimObjectHeadTrack`** (`psiBuildMatrixPalette`, no osso 5, com `anim_flags` 0x10 ou 0x20; ligado por
   `AnimObjectSetHeadTracking`, que a IA chama para olhar o jogador ou quem fala): gira a cabeça para o alvo (um
   ponto, ou com 0x40 o osso `head_bone` de outro objeto) em volta de dois eixos tirados da pose (a direção da
   coluna, osso 2, à cabeça, e o lado), até 1,1 rad de giro (0 se o alvo está a mais de 2,6 rad, atrás) e de -1 a
   0,6 rad de inclinação, andando por `Ang_RotateToBearing` com `head_speed`; o quatérnio da cabeça no mundo vira
   giro x inclinação x o da animação (os produtos da VU0). Nas peles com morphs, os olhos levam o que a cabeça ainda
   não girou: `eye_x = 255 x clamp(0,5 + 2 x (alvo - atual))`, e `eye_y` o mesmo invertido para a inclinação (127 é o
   meio). Sem 0x10, a cabeça volta ao meio e, chegando, apaga 0x30 e zera os olhos.
5. **`AnimObjectDraw`**: de perto (`AnimObjectIsClose(obj, 0xc)`) ou numa cena, escolhe os 8 maiores pesos, tira os
   quase nulos (1e-6) do fim e os põe em ordem de alvo: `Morph_Count`, `Morph_Index[22]`, `Morph_Weight[22]`.
6. **`psiDrawSkinObjectMatrix`**: com o bloco 0x2B na malha, `Morph_Count != 0` e, fora de cena, o menor byte de
   closeness até 2, o `SkinIt` soma `Morph_Weight[k] x delta[m + Morph_Index[k]]` a cada vértice com índice de morph
   antes do skinning da VU0. No PC os pesos vão para o registro do quadro e o vertex shader faz a soma (docs/port.md,
   "Rostos").

Quirks mantidos: o byte `closeness` de cada câmera (`View_AddObjects`, a distância² / lod_scale / 4) só é escrito
pelas câmeras que capturam; no modo de um jogador as câmeras 1 e 2 nunca capturam, os bytes delas ficam em 0 e o
menor é sempre 0: todos os rostos contam como "de perto" (decodificam, piscam e mostram os morphs) a qualquer
distância. E os olhos com `eye_x = eye_y = 0` (quem não acompanha nada com a cabeça) põem os alvos 13 e 12 em 1: os
olhos ficam para baixo e para a direita do personagem (-x do rosto). É o que o código faz; a conferir no
PCSX2 ou num PS2 (os personagens perto do jogador costumam estar com a cabeça no jogador, com os olhos no meio).

Port: `AnimFacialUpdate`, `AnimFacialBlend`, `AnimObjectHeadTrack`, `Drone_AlertStatus`, `Ang_RotateToBearing` e
`Plane_PutPntOnPlane`. Divergências: um script nulo de `AnimScriptAppend` (lista cheia, 32) ou um alvo nulo no
acompanhamento (o original escreveria ou leria endereços baixos) contam em `g_anim_face_guards`.

### Mira do tronco (portada, `src/engine/anim.cpp`)

`AnimObjectSetAiming` (a IA, em `NDrone2_DoTracking`; o jogador só no multijogador, em
`Player_StandAtNewPosition`) liga `anim_flags` 0x80 com um alvo (um ponto, como o `target_pos` do drone, ou com 0x200
o osso `aim_bone` de outro objeto animado). `psiBuildMatrixPalette`, logo depois de montar a coluna (osso 2), chama
`AnimObjectAimAt`, que gira a coluna no mundo para o cano da arma apontar para o alvo; os ossos seguintes (braços,
mãos, a arma) já nascem girados:

1. Fora de cena, só com o objeto visto por alguma câmera (um byte de closeness abaixo de 0xff). Apaga 0x7000 e liga
   0x100 (mirando). Sem arma (`weapon_data` +0x50, o osso do cano, 0xff) nada; no multijogador, também não com o byte
   +0x84 da arma zerado.
2. O cano: o anexo "cano + 3" da pele (ou "+ 1"; sem os dois, o id 0x41, que não existe: zero e identidade), na pose
   da animação sem a mira (`AnimGetBoneWorldTrans2`: monta a cadeia do anexo até a raiz pelas matrizes do quadro, já
   que os ossos depois da coluna ainda não existem); aponta para -x da matriz dele.
3. Os eixos saem do pai da coluna (a bacia): "cima" da bacia à coluna, "lado" = cima x cano. Com o alvo a menos de
   `aim_dist_sq` da bacia, liga 0x1000 e mede dois ângulos, cada um num plano pela bacia: com cima e o cano
   (inclinação) e com o lado e o cano (giro). Em cada plano projeta a arma, um ponto 100 m à frente do cano e o alvo;
   com a arma mais perto da bacia que o alvo, a reta do cano corta a esfera centrada na bacia que passa pelo alvo, e o
   ângulo é o que leva esse ponto ao alvo (acos; o sinal pelo produto misto com o outro eixo). Fora do jogador só
   alvos à frente (cosseno >= 0). Inclinação entre -0,9 e 0,9 rad (o jogador: -pi/2 e 1,2708), 0x4000 quando presa;
   giro acima de -0,9, 0x2000 quando preso. Um alvo projetado a menos de 0,5 m da bacia num plano zera o ângulo do
   outro. O jogador só inclina.
4. `aim_yaw`/`aim_pitch` (+0xf4/+0xfc) andam até eles por `Ang_RotateToBearing` com `aim_speed` (0,2 na IA), e o
   quatérnio da coluna no mundo vira giro(cima) x inclinação(lado) x o da animação. Sem 0x80, o tronco volta ao meio
   e, quase lá (0,01 rad), apaga 0x180.

Com 0x1000 ligado, `DroneWeap_Ready2Fire` passa a conferir se o cano aponta para o alvo (20 graus; 30 no DTYPE 0xd)
antes de cada tiro; antes do porte o bit nunca ligava e a IA atirava com o cano onde a animação o deixava.
Peculiaridade mantida: o limite de cima do giro compara a inclinação (`0,9 < inclinação`, nunca verdade fora do
jogador, que não gira): o giro só é preso por baixo. Divergências: o w do cano é lixo da pilha no original (0 aqui);
com a mira pedida e o alvo nulo (o original leria o endereço 0; `AnimObjectSetAiming` não deixa isso acontecer) não
mede e conta em `g_anim_aim_guards`; `AnimGetBoneWorldTrans2` monta as matrizes num vetor de 128 (o original, 96 na
pilha, indexadas pelo osso). O relatório da captura tem `mira_ia mirando N no_alcance N guardas N | perto ... giro_tronco
inclinacao flags erro_cano` (o ângulo entre o cano e o alvo, em graus); `--no-torso-aim` (depuração) desliga.

### Entrada (portado, `src/engine/input.cpp`)

O jogo não lê botões: cada jogador tem 40 "ações" (`PlayerSetting`, 0x158 bytes por jogador), cada
uma um valor de -1 a 1. A plataforma (`psiInput_MapInputs`) preenche os valores conforme a
configuração de botões; `Input_Update` passa os analógicos pela curva de resposta
(`StickCompensation2` com `MapAnalogStick`: zona morta até 37/127 e trechos lineares; o andar passa
por `StickCompensation3`, que leva ao máximo um eixo além de 0,707 com o outro perto do centro),
nega a visão vertical se a opção de inverter estiver ligada e calcula o estado de cada ação: 1
ligada, 4 acabou de ligar, 8 repetição (depois de 46 ticks, a cada 8). Uma ação pode ficar travada
até ser solta (menus). O jogo consulta com `Input_Action` (valor x 100 se o estado tiver um bit da
máscara) e `Input_Actionf`.

Na configuração 0: 0 virar e 3 mirar (analógico direito, horizontal), 1 de lado e 2 andar
(esquerdo), 4 mirar e 5 olhar (direito, vertical), 7 pular (triângulo), 8 agachar (X), 9 atirar
(R1), 14 usar ou recarregar (quadrado ou R2), 19 mirar (L1), 30 pausa (Start), 26-29 direções nos
menus. As oito configurações (`PlayerSetting::style`: NightFire, Moonraker, Octopussy, Goldfinger, Dr. No,
Thunderball, GoldenEye, Classic Bond) são um `switch` em `psiInput_MapInputs`: cada uma escolhe que
analógico anda e qual olha e que botão faz cada ação (o valor de um botão é o próprio bit,
`(float)(botões & máscara)`, que `Input_Update` limita a 1); depois vêm as ações comuns (Start, os botões e
as direções dos menus, o analógico esquerdo também como direcional). `psiInput_PollDevices` lê os slots
da libpad (`tSlot`, 8 de 0x180 bytes) e aplica uma zona morta de 40/127 em cada eixo (a curva escrita
antes em +0x148 é apagada pela linha seguinte, no original). A vibração: `Input_RumbleStart(jogador,
ticks, força)` (com a opção de vibração e `GameState` +0x51) -> `psiInput_RumbleStart` (motor pequeno
ligado, o grande com a força x 2,5, a duração contada em `psiInput_MapInputs`; o jogo pausado desliga
todos). `Input_ChangeControllerStyle` troca o estilo e zera as ações do jogador; a aba CONTROLS da pausa
mostra o mapa de cada estilo com `Menu_DisplayControllerStyleList` (símbolos `~A`... da fonte e os
rótulos do `*Txt.dat`). No PC (`src/platform/psi_input.cpp`, `psi_pad.cpp`; docs/port.md, "Controles"), o
controle XInput entra nos slots do PS2 e o teclado e o mouse, por uma tabela de teclas, somam-se ao jogador 0.

### Jogador (portado em parte, `src/engine/player.cpp`)

O Bond é um objeto do tipo 3 criado por `Player_Init` no ponto de partida: os dados do tipo
(`BLData`, 0x970 bytes) guardam velocidades, inclinação da visão, ar, vida, armas e o teste de
colisão do corpo; o objeto de animação (`obj_tag::sphere_src`) guarda o chão sob os pés e as flags
de contato. `Player_StandAtNewPosition` procura o chão até 3 m abaixo e põe `obj_tag::pos` 1,1
acima dele: a posição do jogador é o quadril, e os olhos ficam 0,7 acima (1,8 m do chão).

A cada tick (`Player_Update`, a atualização do tipo 3):

- o subestado move o jogador. Andando (`Player_SSWalk`) e agachado (`Player_SSCrouch`, 75% da
  velocidade), `Player_Move` lê andar, andar de lado e virar: para a frente a velocidade cresce
  com o quadrado do analógico até 0,1 m por tick, para trás e de lado é menor, tudo suavizado 20%
  por tick; o giro passa por `AccelFunc0`, que acelera de 0,04 a 0,08 rad por tick depois de dois
  segundos com o analógico no fim (e, por uma peculiaridade, também sobe a até 0,12 com o analógico
  a meio caminho depois de acelerar). Na água vira nado (abaixo); pular (`Player_HandleJump`) precisa do pé
  no chão e de 0,95 m livre acima e sai a 3,9 m/s;
- `Player_Aiming` inclina a visão (analógico direito, até 90 graus para cima ou para baixo) ou, com
  a mira ligada, move a mira na tela e só gira a visão nas bordas;
- a velocidade local vira deslocamento no mundo, e o giro, ângulo;
- `Player_Collision` aplica a gravidade (9,8 m/s², até 45 m/s) fora do chão e monta a cápsula do
  corpo: um segmento de 0,275 m acima do quadril até a altura dos pés mais o raio, com raio 0,55.

O `Collide_Update` empurra a cápsula para fora das paredes e dos objetos, e
`Player_CollisionHandler` (a colisão do tipo 3) aplica o empurrão, procura o chão com um raio para
baixo (`Player_FeetOnPoint`: chão a até 0,1 m abaixo dos pés conta como pisado, e sobre um objeto
que se move o jogador anda junto), conta o tempo de queda (mais de um segundo machuca) e trata os
objetos tocados. Usar (`Player_Activate`, pela ação 14) procura objetos numa esfera de raio 1 a um
metro à frente dos olhos e aciona portas, interruptores, cadeados...

Dano e morte. Um tiro que acerta o jogador passa por `MP_RegisterBulletHit` (fora do multijogador
todo acerto vale) e `Player_HandlePain`: o dano é multiplicado pela dificuldade (`Plr_DMod_*`, que o
`ReadTuningVars` grava a cada carga pelo grupo de fases: 07000012 0,5/0,6/1,0, o castelo 0,6/0,7/1,0...; os do .data,
fácil 0,75, normal 1, difícil 1,25, só valem antes da primeira carga; ver "O ajuste das fases"; o nível 4 em dobro), o
colete (`BLData::armour`) absorve o que
puder dos tiros (quedas e danos contínuos vão direto para a vida), e o resto sai da vida; a trapaça de
invencível (`CheatInfo` +0) mantém a vida. Cada ferimento soma um vermelho na tela
(`BLData::pain_flash`, dano x 21,3 até 128 por vez, até 255). Uma vez por tick
`Player_CheckForDeath` prende a vida entre 0 e 500 e, zerada, mata o jogador: o objeto vira o tipo
0x12 no estado 2, sem arma nem mira, no subestado 0xd, e a missão falha (canal 0x5e e a música da
morte). Morto, `Player_HandleDeath` liga a cortina de sangue do HUD (pane 0xc) e, quando ela cobre
a tela (~7,5 s) ou o jogador aperta Start, liga o canal 0x62, que `Mission_Update` trata (ver "HUD e
missão"; Enter é o Start).

A câmera do jogador é posicionada por `Player_PositionCamera`, ligada à câmera 0 por
`Camera_SetUpdator`: em primeira pessoa, a rotação do jogador inclinada pelo pitch, nos olhos
(`Camera_SetToPlayer`). Agachado, os olhos descem 0,45 m aos poucos.

Água. Uma sala tem água quando uma célula 0x8017 está dentro dela (`cel_tag::water_y`; 07000002 tem 5 salas com água,
07000016 uma, o mar de Drake). Com o quadril a menos de 0,3 m acima dela, `Player_InWater` troca andar ou agachar por
nadar (subestado 3): `Player_Move` leva a velocidade pela inclinação da visão (olhar para baixo e andar mergulha) e
`Player_Update` não deixa subir acima de 0,3 m sob a superfície; sair da água em pé volta a andar. `Player_MonitorAir`
(a cabeça, osso 5, abaixo da água) mostra o fôlego no HUD (painel 5), tira 5 de ar por segundo (volta 20 fora) e, sem
ar, 2 de vida a cada meio segundo, com os sons de mergulhar e de sair. A câmera debaixo d'água passa por
`Camera_Shear` (de `Camera_Update`): uma fase anda `REC_FRAME_RATE` por chamada e a matriz da câmera ganha um
cisalhamento de 0,025 x (cos, sen) da fase, escalado por (1 + 0,025 cos, 1 + 0,025 sen, 1 + 0,025 cos), com a posição
como estava, e `GlobalBlur` = 0x91 (o borrão da tela). Os passos na água rasa (material 13 ou 14) soltam uma fumaça de
marola (`Env_WaterRipple`, de `Player_DealWithObjHit` e `NDrone2_DealWithObjHit`). O PS2 não tem névoa própria de
debaixo d'água: a névoa da fase continua. Peculiaridade: a cabeça vem da pose do corpo, então um jogador posto direto
na água sem nunca ter andado (`--at` dentro dela) não tem pose (`AnimGetBoneWorldTrans` dá zero) e não se afoga; no
jogo ele sempre entra andando.

No PC as funções `psi*` de desenho (`src/platform/psi_draw.cpp`) registram cada
`psiDrawObjectMatrix` com a câmera em uso (`psiUseCamera`) e o estado de mistura; o renderizador
desenha o registro da câmera 0.

### Armas do jogador (portado, `src/engine/player_weapon.cpp`)

As 115 definições de arma (`weapon_definition_tag`, 0x10c bytes em `weapon_data`) e os 34 tipos de
munição (`ammo_data`) vêm do ACTION.ELF do usuário: a primeira é montada por um construtor
estático, que o port roda num interpretador na partida (ver `docs/port.md`). Cada definição tem a
classe (o espaço no inventário), a pele com os braços e a arma, os scripts de animação (sacar,
guardar, atirar, recarregar, mirar, esperar), o pente, a munição, a cadência, quantos projéteis por
disparo, o alcance, o zoom e a posição na tela.

`Player_InitWeapon` (de `Player_Init`) cria a arma na mão, um objeto só da câmera do jogador, e dá
as armas da fase: cada fase da campanha tem a sua lista (a pistola, o taser, o relógio...), as que
continuam a anterior recebem o inventário dela (`Player_RamLoad`, depois de um reinício quente), e
as melhorias compradas (`Upgrade_Weapon`) trocam a pistola, os rifles e os aparelhos pela versão
melhor. `Player_EquipWeapon` enche o pente de uma arma nova e manda o resto para a munição guardada
do tipo.

A cada tick, `Player_WeaponRecoil` calcula o balanço da arma (ao andar e um lento sempre) e roda a
máquina de estados da arma na mão (`Player_SetWeaponAnimObj`): parada (com animações de espera
depois de 20 s sem mexer nos controles), guardando a anterior, trocando o modelo
(`Player_SetWeaponAnim`, com a manga do terno do jogador) e sacando, recarregando, atirando,
mirando. `Player_Weapon` lê os controles: mirar (a mira se move na tela e a arma gira junto; a
luneta esconde a arma), o zoom (`Player_Zoom`), trocar de variante ou de modo de disparo, trocar de
arma ou de aparelho (`Player_WeaponChange`, pulando as que o jogador não tem ou sem munição) e a mira
automática. `Player_WeaponFiring` atualiza os visores presos à arma, usa ou recarrega com a ação 14,
recarrega sozinho com o pente vazio e atira: um aperto dá uma rajada (`burst` tiros do modo, um a
cada `fire_delay` ticks, enquanto o gatilho estiver seguro), e cada tiro toca a animação e chama
`Player_WeaponInitBullet` na hora ou no quadro `fire_frame` dela. Esse procura com um raio dos olhos
até 256 m na direção da mira o ponto atingido (para o clarão, `Player_MuzzleFlash`) e cria os
projéteis com `Bullet_init`, dos olhos (ou de entre os olhos e o cano) nessa direção.
`Player_PositionGun` põe a arma nos olhos, na rotação do olhar.

Mira automática. `Player_AutoAim` (de `Player_Weapon`, a cada tick), com a opção "Auto aim" (`PlayerSetting::autoaim`,
+1; o multijogador tem a sua) e sem mirar, passa cada drone e cada jogador por `Check_AutoAim` (com as armas
0x50/0x51, o gancho, a opção não importa e os candidatos são os objetos 0x51, até o alcance da arma). Ficam de fora: o
objeto desligado (+0xfc), o drone morto, da classe 0xb, no estado 1 ou que não é inimigo (`Drone_tag::side` 1; no
multijogador, o do mesmo time), o próprio jogador e os mortos. O ponto mirado é a coluna (osso 2) do drone ou do
jogador, ou a posição do objeto, até o melhor encontrado (`Autoaim_Range`, 25 m, no começo). O cone (as constantes
`Autoaim_*`, que o `ReadTuningVars` grava a cada carga; as do .sdata, 10 graus, 35 m, x 2 e x 1,25, só valem antes da
primeira) é de 0,12 rad de lado e 0,22 rad de altura (`Autoaim_Angle_H/V`) x a mira automática da arma
(`weapon_definition_tag` +0x14, em %: 100 nas pistolas, 40 a 85 nos rifles) x (1 - distância / 25) x o fator da
dificuldade (Operative 1,9, Agent 1, 00 Agent 0: sem mira automática), 1 inteiro com o gancho e x 1,4 para o alvo atual
(`Autoaim_LockOnMul`). Dentro dele
(giro por atan2 contra `obj_tag::rot.y`, inclinação por asin contra `BLData::pitch`) e com linha de visada, o alvo vira
`BLData::aim_target` e a posição dele na tela (`View_3DPoint2Screen`) vai para a mira suavizada (`aim_cur_x/y`, em
quartos da tela; `Player_AutoAim` corta pela metade no mesmo tick), que a cruz do HUD, a arma (`Player_PositionGun`)
e a direção da bala (`Player_GetAimingPoint`) seguem. Sem alvo, a mira volta ao meio. O jogo do PS2 não tem mouse e não
desliga a mira automática por aparelho; no PC ela vale igual para o mouse e o controle (docs/port.md).

Laser e relógio. As pistolas melhoradas e as armas com `weapon_definition_tag::flags` 8 (6-9, 0xf, 0x17, 0x18)
têm a mira laser (`Player_LaserPointer`, de `Player_CollisionHandler`), ligada e desligada pelo evento 4/1 da
animação da arma (`BLData::gun_flag8`): um raio dos olhos até 100 m à frente do anexo 4 da arma; o feixe (o primeiro
objeto de efeito, `BLData::fx[0]`, o modelo 0x2000133 com alfa andando entre 0x10 e 0x30, ou 0x2000131 na visão
especial) vai do anexo até o ponto do raio mais perto do acerto, só na câmera do jogador em primeira pessoa, e o ponto
(`BLData::glow`, vermelho) fica 1 cm à frente da superfície, alinhado à normal, maior com a distância (até 4x). O
laser do relógio (armas 0x4e/0x4f, `flags2` 0x100) desenha a cada tiro o raio do cano até o ponto atingido
(`Draw_LaserBeam`, de `Player_MuzzleFlash`: o modelo 0x2000131 esticado em z, opaco, em todas as câmeras); o
aquecimento dos cadeados de laser é dos projéteis dele (`Lock_*`). O taser (`flags2` 0x80, quando acerta) faz um raio
em zigue-zague de 2 a 32 segmentos (0,8 por metro, até o alcance da arma 0x4c) nos objetos de efeito, com os pontos do
meio sorteados por divisão (`Draw_TaserBeam`, `FractalThing`). Os raios são modelos 3D esticados (`PositionBeam`), não
triângulos soltos nem partículas. Com uma arma da pele do relógio (0x500004b: 0x38, 0x4e, 0x4f, 0x5a, 0x5c),
`Player_SetWatch` põe o mostrador (anexo 2, entidade 0x20008f9) e cria os três ponteiros (0x2000876/7/8), girados em
volta de x pela hora: a hora de partida da missão (9:00 em 07000001-04, 19:30 em 05-08, 22:30 em 09-0b, 8:00 em
0c-0d, 6:00 em 11-13 e 4a, 4:00 em 14-16) mais o cronômetro da missão; com outra arma eles são apagados.

### O ajuste das fases (portado, `src/engine/resetmap.cpp`)

`ReadTuningVars` (0x1d3a80, 0xe00 bytes) roda a cada carga, no fim do `ResetMap_Load` (depois de criar os objetos e
antes do `Player_Start`). Apesar do nome não lê arquivo nenhum: são constantes do código, montadas em registradores e
gravadas nas globais. Sempre: a velocidade da mira e do giro do jogador (`Plr_AimSpeed_X/Y` 0,1275/0,136, contra
0,15/0,16 do .data; os outros `Plr_AimTurnSpeed_*`, `Plr_ScopeSpeed_*`, `Plr_NoAimTurnSpeed_*` iguais ao .data), a
vida mínima ao recomeçar (`ContinueHealthBoost*`, 50) e a mira automática (`Autoaim_*`: ver "Armas do jogador").
Depois um `switch` por `GameState.level_file` (tabela de saltos em 0x305160, 07000001 a 0700004c) grava, por grupo de
fases, os 32 valores dos personagens e o dano que o jogador recebe:

| Fases | dano recebido (`Plr_DMod_Easy/Normal/Hard`) | precisão dos personagens (`DroneFiring_Accuracy_*`) | outros |
|---|---|---|---|
| 07000001-04 (Henderson) | 0,5 / 0,7 / 1,2 | 0,5 / 0,7 / 1,0 | o conjunto padrão |
| 07000005-08 (castelo) | 0,6 / 0,7 / 1,0 | 0,8 / 0,9 / 1,3 | |
| 07000009-0b (torre) | 0,6 / 0,8 / 1,0 | 0,8 / 1,0 / 1,0 | |
| 0700000c-0d (usina) | 0,4 / 0,7 / 1,2 | 0,6 / 0,8 / 1,2 | alvo que acabou de parar: 1 s e 0,1; capitães x 1,5 no dano e na precisão |
| 07000011-13, 0700004a (segunda torre) | 0,5 / 0,6 / 1,0 | 0,7 / 0,8 / 1,0 | dano que o jogador causa 1,5 / 1,2 / 1,0 |
| 07000014-16 (base) | 0,4 / 0,6 / 1,0 | 0,5 / 0,7 / 1,0 | |
| 0700001b (estação) | 0,6 / 0,7 / 1,0 | 0,6 / 0,8 / 1,2 | dano que o jogador causa 1 / 1 / 1 |
| 07000021-29, 4b, 4c (multijogador) | não grava: `Plr_DMod_Multi/Head` 4, `LowerLimb/UpperLimb` 0,8 | 0,8 / 1,0 / 1,5 | dano causado 2 / 1,5 / 0,5, capacete 0,5, "de perto" 4 m |

O conjunto padrão dos personagens (Henderson e a base o gravam inteiro; os outros mudam só o que a tabela diz): o dano
que o jogador causa por dificuldade 1,5 / 1 / 0,8 e por parte do corpo cabeça x 10, pernas 0,75, braços e tronco 1 (o
.data tem cabeça x 100 e 2 / 1,5 / 0,5); as armaduras capacete 1 (.data 0,5), combate 0,25, jaqueta 0,5, colete 0,75; o
ritmo das rajadas 15 / 45 / 60 ticks entre 4 e 30 m; 2 s de visão antes de acertar; os fatores do alvo que começou a
andar (0,25), anda (0,75) e parou (1); "de perto" a 3 m (.data 4) com precisão e dano x 2, pelas costas x 2; os capitães
x 2. As fases fora do `switch` (o menu principal, as cenas 0700000e-10, as de teste, os pacotes 071xxxxx) ficam com os
valores da carga anterior. A divisão "conjunto padrão + diferenças" é a do nightfire-cxbx (NightfireResearch); cada
valor foi conferido store a store na desmontagem, e o `nf_loader_check` roda o `ReadTuningVars` do ELF no interpretador
MIPS para 0x07000000-0x0700004f e três outros códigos, com as 65 globais envenenadas dos dois lados, e compara bit a bit
(`ajuste das fases (ReadTuningVars): 83 de 83 conferem com o ELF`). A página de depuração `P_TWEAKS` muda as mesmas
globais; a carga seguinte as regrava, como no PS2.

### Itens (portado, `src/engine/pickup.cpp`)

Munição, armas, colete, chaves e itens da missão são objetos do tipo 0x2f com um `PICKUPINFO` (0x40
bytes) nos dados: velocidade da queda, o deslocamento da matriz até o centro da esfera, o estado (0
caindo, 1 parado, 2 esperando reaparecer), o tipo, o item, a quantidade, o canal de switch, o som, o
tempo para reaparecer (em unidades de 10 s; 0 some, 0xffff fica), a vida útil em ticks, o texto da
mensagem e se gira. Os tipos, pelos próprios textos do jogo (`USATxt.dat`, "Picked up %s"...):

| Tipo | Item | Ao pegar |
|------|------|----------|
| 0 | arma | `Player_EquipWeapon` (já melhorada por `Upgrade_Weapon`); se já tem a classe, vira munição |
| 1 | munição | `Player_EquipAmmo` da arma do item |
| 2 | chave de código | `Player_EquipKey`: `BLData` + 0x70c + item (as chaves das fechaduras, `Lock_*`) |
| 3 | colete | o colete vai a 50 se estiver abaixo; nos bots, +20 de vida e colete |
| 4 | item da missão | só a mensagem (texto próprio) e o canal de switch |
| 5 | invisibilidade | `BLData::visibility` = 0 |
| 6 | ficha "007 Bonus" | só existe com a medalha de ouro da missão (`PlrStats_HasGoldMedal`); conta nas estatísticas |
| 7-11 | arma do conjunto da partida (multijogador) | vira um tipo 0 na criação (`PickupMatrix`) |

Não há item de vida. Os itens nascem de três jeitos: posicionados na fase (tipo 0xf0 de
`parsemap_create_dynamic_objects`, parâmetros 0-6, já parados no lugar), soltos por quem morre
(`Pickup_CreateSimple`: a arma da mão de um personagem, `DroneWeap_DropWeapon`, ou a do jogador no
multijogador; cai com a gravidade, pousa pela base do modelo e some em 2 min) ou criados por scripts
e torretas (`Script_CreateEntity`, `GT_Update`, ainda não portados). `Pickup_Create` liga a flag
0x800 (o tweak do desenho é a cor da luz da sala, `obj_tag::color`), tira a camada de sobreposição e
põe o colete para piscar (0x80000); a ficha e os itens do multijogador giram a 1 rad/s.

A cada tick `Pickup_Update` (o único callback do tipo em `control_funcs`) faz o item solto cair (um
raio da posição anterior até a base do modelo procura o chão; sobre outro objeto herda o movimento
dele) e procura um jogador vivo (estado 1, não morto) a menos de raio + 1,1 m do centro da esfera, com
linha de visão (no multijogador também os bots). `Pickup_Handler` entrega o item; se nada entrou
(munição cheia, colete já em 50, modelo da arma fora da fase) o item fica. Depois a mensagem na tela,
o canal de switch, o som (0xf5 armas e munição, 0x141 colete, 0x14c ficha) e o destino do item.
Os itens perto da câmera pulsam com `FlashMod` (entre 0,5 e 1 a 1 Hz), calculado por `Check_Target`,
que também acha o objeto na mira do jogador (`BLData::target`), avisa o personagem que está na mira
(`Drone_tag::unknown_2a`), marca a mira sobre aliados e civis com arma letal e mostra as mensagens de
porta e de gatilho de carga.

### Projéteis (portado, `src/engine/bullet.cpp`)

Todo tiro que não é instantâneo na tela é um objeto do tipo 5 (balas, foguetes, granadas, dardos,
minas, o gancho, o laser de armadilha), criado por `Bullet_init(jogador, quem atirou, dono,
definição da arma, posição, direção, som)`. Os dados do tipo (`BU_tag`, 0x110 bytes) guardam a
direção, quem atirou, a definição da arma (`weapon_definition_tag`: velocidade, alcance, dano,
dispersão, modelo e as flags do projétil em +0x6c e +0x70, ver `player_weapon.h`), o teste de
colisão do tick, a distância percorrida,
a velocidade e um timer (pavio, contagem). A direção recebe a dispersão da arma (mais o acúmulo dos
tiros seguidos do jogador; zero para quem mira com certas armas).

A cada tick (`Bullet_update`): no primeiro, um passo de 0,5 a 1 x a velocidade e o modelo (ou o
traçante esticado pela velocidade); depois, a velocidade da arma por tick a 60 Hz, com gravidade,
giro, aceleração, teleguiado (`Bullet_homing`: o alvo mais próximo num cone à frente, virando no
máximo 0,01 por componente por tick) ou o míssil pilotado pelo stick do jogador (flags2 4: o Sentinel 0x2c em
"Guided"; o analógico direito vira e inclina até pi/128 rad por tick, sem passar da vertical, com rolagem; no PC o
mouse também, docs/port.md "Controles"). O último passo é
cortado no alcance da arma e o tiro some no tick seguinte. A bala monta o teste do trecho que vai
andar: um raio de pelo menos 1 m a partir da posição antes do passo (ou uma esfera, se o passo for
pequeno). `Collide_Update` roda o teste, copia cada acerto para a lista do objeto atingido com o
dano da arma (`HITDATA_tag::unknown_08`; é assim que jogador, drones, portas e quebráveis recebem o
tiro) e chama `Bullet_CollisionHandler`. O primeiro acerto sólido (vidro, materiais com as flags
0x40/0x80 e objetos marcados deixam passar) para a bala, que é apagada no tick seguinte; conforme a
arma ela também explode, gruda na superfície (minas, dardos, satchel, laser), quica perdendo
velocidade pelo material (granadas, até 20 vezes) ou ricocheteia (até 3 vezes, pela chance do
material). `Bullet_DoTrails` cuida dos rastros e da fumaça, do zunido perto da câmera, do pavio
(explosão, granada de luz que ofusca pelo ângulo e pela distância, granada de fumaça) e da
contagem do satchel, com dois dígitos (objetos do tipo 0x10 que `Debris_Update` apaga no tick
seguinte).

As cápsulas ejetadas são 32 objetos reaproveitados em círculo (`ShellCasings`, criados a cada fase
por `Bullet_InitCasingLists`): `Bullet_init_casing` pega a próxima para a munição da arma e, depois
do atraso da arma, ela sai do osso da arma (`Bullet_init_casing_ex`), cai com a gravidade, toca o
som do material ao passar do chão e some depois de 4 s. As explosões estão em "Destruição", abaixo;
os sons ainda são stubs. `nf_loader_check` dispara 8 balas da pistola em leque dos olhos do jogador em cada fase
(`balas: criadas=8 acertaram=N`).

### Efeitos de impacto e fumaça (portado, `src/engine/effect.cpp`)

Cada material de colisão (`HITDATA_tag::surface & 0x3f`, 23 materiais: carpete, papel, madeira
fina e grossa, metal, vidro, água...) tem uma entrada em `EffectInfo` (0x88 bytes, `HEINFO_tag`,
copiada do ACTION.ELF na partida): a marca de bala, as marcas de pé, os sons (bala, passo, corpo),
o ricochete e o quique, três fumaças, os detritos e o emissor de faíscas. `Effect_Create` recebe o
acerto e o objeto que acertou: com a flag 0x20 cria as faíscas e os detritos (`Debris_Create`,
objetos do tipo 0x10 que voam e somem); depois, pela flag, o efeito de bala (`Effect_Bullet`: som,
fumaça e poeira do material, sempre nos tiros do jogador e metade das vezes nos dos outros, e a
marca), de passo (`Effect_Player`: a pegada sob o pé), de laser ou de corpo.

As marcas são anexos (`ATTACH_tag`, 0x60 bytes) de uma lista de 200 (`EffectList`, uma `DList`
de nós livres e em uso): a marca vai para a lista de anexos da sala atingida, 1 cm fora da
superfície e virada para a normal, ou, num objeto, para a dele, em coordenadas do objeto (anda com
ele e volta à lista quando ele some, `Effect_RemoveSObjs`). Acabando a lista, a mais antiga é
reaproveitada. O desenho delas é o dos anexos (`View_DrawCelGlist`, `View_DrawObjectGlist`).

A fumaça (`Gas_Init`, objetos do tipo 0xe atualizados por `gases_smoke`) cresce, gira e muda de cor
e transparência ao longo da vida (a cor do objeto vira o "tweak" do desenho, em
`View_SetupRenderModes_Tweak`); pelo tipo ela sobe sem passar do teto da sala, segue o vento da sala
(`Env_GetWindVel`), cai, ou fica parada soltando mais fumaça. O clarão do tiro (`Draw_MuzzleFlash`)
é o modelo da arma no objeto do clarão, no osso do cano, por três ticks; os rastros (`Swoosh`, tipo
0x41) são um modelo esticado entre dois pontos, virado para a câmera, que afina e some.

Os modelos desses efeitos trazem no próprio pacote do GS o modo de mistura (escritas A+D de
`ALPHA_1`, `TEST_1` e `ZBUF_1` antes dos triângulos): o clarão e a fumaça são aditivos e não
escrevem Z, a marca de bala é subtrativa (escurece a parede). O renderizador usa esse estado
(`docs/port.md`, "Estado do GS, tweak e partículas").

### Destruição (portado, `src/engine/break.cpp`, `src/engine/explode.cpp`, `src/platform/psi_shard.cpp`)

Quatro tipos de objeto, criados pelos posicionamentos e atualizados por `control_funcs`:

- **Quebrável** (tipo 0x20, `Break_Create`, `BREAK_tag` de 0xc bytes: tipo, resistência, máscara de
  classes de tiro). `Break_Update` lê os acertos do tick (`Collide_Update` os copia para a lista do
  objeto, com o dano da arma em `HITDATA_tag::unknown_08`): `Collide_FilterBullets` descarta os tiros
  cuja arma tem em `weapon_definition_tag::stats` (+0x10) um bit da máscara (mais 0x106, sempre); os
  outros tiram o dano da resistência. Em 07000001 um personagem que atravessa um vidro tira 20 x o
  quadrado do quanto andou no tick. Abaixo de zero, `Break_Kill`: som (0x2e no vidro), e então o vidro
  (tipo 2) se estilhaça e os outros soltam detritos do material (`Debris_Create_Loop`: um a cada três
  triângulos de colisão, no centro do triângulo) e fazem os inimigos de 07000001 comentarem
  (`Drone_BrokenObject`). Explosões tiram resistência por `Break_ApplyDamage`; os scripts quebram o
  quebrável que contém um ponto (`Break_DoBreak`).
- **Vidro e estilhaços**. Na carga, `psiCreateBreakableData` lê a cadeia de DMA da caixa 0 do modelo e
  guarda, num registro de 0x18 bytes pendurado no terceiro campo do cabeçalho do bloco 0x2d, os
  vértices (posição, UV e cor como float na escala do GS, `SHATTER_VERT` de 0x40 bytes) e um triângulo
  (textura e três índices) para cada vértice de tira sem o bit ADC. Como no original, os índices contam
  a partir do começo do lote: nos modelos com mais de um lote, os triângulos dos lotes seguintes usam os
  vértices do primeiro. `ShatterPolyRecurse` corta cada triângulo ao meio pela aresta mais longa, num
  ponto sorteado entre 25% e 75% (posição, UV e cor interpoladas), até a aresta mais longa ao quadrado
  não passar de metade do quadrado da distância do centro dele ao tiro, entre 0,5 e 100: pedaços
  pequenos perto do tiro, grandes longe. Cada pedaço vira um estilhaço (`SHARD_tag`, 0xa0 bytes, na
  `ShardList`) com uma cadeia de DMA própria (`psiCreateShard`): parado por distância x 15 ticks, depois
  voa na direção do tiro a `min(0,125 / distância, 0,1)` m por tick, gira ao acaso e cai com a gravidade
  (`UpdateAllShards`), por 25 ticks. O centro vem de `obj->pos` mais a média dos vértices, sem a rotação
  do objeto (como no original). `DrawAllShards` desenha cada um (`psiDrawShard`) com a matriz da posição
  e da rotação; o estado é o de `psiSetShardRenderStates` (Z testado sem escrita, alfa > 0, mistura
  transparente). O alfa que `DrawAllShards` põe no tweak não chega ao estilhaço: o pacote da VU1 de
  `psiDrawShard` escolhe a rotina 0 (`ROTATE_FAST`, cor do vértice sem tweak).
- **Destrutível** (tipo 0x21, `Destroy_Create`, `DESTROY_tag` de 0x18 bytes). Parâmetros: canal de
  saída, canal de entrada, explosão, resistência, máscara de classes de tiro, raio do dano (/100) e
  atraso (segundos). Tiros tiram resistência (`Destroy_CollisionHandler`; a classe 0x67 esquenta o
  objeto: o verde e o azul do tweak caem 5 por tick com o acerto e voltam 1 por tick sem); abaixo de
  zero, ou quando o canal de entrada liga (`Destroy_Update`), `Destroy_Smash`: troca para o modelo
  quebrado (o `lod` da entidade ou a entidade seguinte), tira as marcas de bala, liga o canal de saída
  (na hora ou depois do atraso, contado por `Destroy_Update`) e, com explosão, `Explode_Create` no centro
  da esfera (tipo 1: script 0x6000027 e raio x 4; tipo 2: 0x600004f e x 2; outros: 0x6000052 e x 3;
  dano tipo x 10). Sem canal o objeto vira tipo 1 (sem atualização).
- **Zona que machuca** (tipo 0x27, `Hurt_Create`, `HURT_tag` de 0x1c bytes: tempo, velocidade,
  caminho, quadros, dano, tipo, uma vez, quem dispara, canal, som). Com canal, aparece e some com ele;
  dispara quando o canal liga ou, sem canal, quando quem a toca está na máscara (8 jogador, 0x10 drone, 2
  bala; 0 dispara sozinha) e então percorre o caminho (`Spline_Interp`), voltando ao começo no fim ou
  sumindo ("uma vez"). O dano é de quem encosta: `Player_DealWithObjHit` e `NDrone2_DealWithObjHit`
  leem `Hurt_GetDamage`.
- **Explosão** (tipo 0x0f, `Explode_Create`, `EXPLODE_tag` de 0x40 bytes: eixo e lado dos detritos,
  sala, quem causou, dano, raio, duração, script, material). Sacode as câmeras (`Camera_Shake`) se o raio
  for de pelo menos 0,5 (abaixo disso não fere) e toca o script do efeito, alinhado ao jogador mais perto
  (`control_ClosestPlayerPnt`). No primeiro tick de colisão (`Explode_CollisionHandler`) o dano se
  espalha uma vez (`Explode_Propagate`): uma esfera do raio (máscara 0xc0) pega os objetos e cada um
  recebe `dano x (1 - distância até a esfera dele / raio)` pelo tipo: personagens a mensagem 9 com um
  `DroneImpact_tag` montado na pilha (+0x00 dano, +0x04 raio, +0x10 centro, +0x20 quem causou;
  `Drone_ExplosiveHit` guarda o atacante), o jogador `Player_Hurt` (`Player_HandlePain` sem tipo), o
  quebrável `Break_ApplyDamage`, o destrutível `Destroy_Smash` (dano de pelo menos 1, sem a máscara
  0x20), projéteis explosivos (armas 0x2b, 0x34-0x37, 0x3a) explodem junto, e helicóptero, torreta,
  sensor, monitor, submarino e multijogador pelas funções deles. `Explode_Update` roda o script, solta
  detritos de metal (materiais 5, 6) ou pedra (8, 10, 11 em 07000008 com o canal 0x53) no primeiro
  quarto dele e apaga o objeto quando ele acaba.

O fogo, o clarão, a luz, a fumaça e os sons de uma explosão estão no script do efeito (0x6000052 tem
165 quadros e 37 fluxos), tocado pelo script player (abaixo): `Explode_Create` põe o raio em
`SCRIPTINFO::scale` (as posições do script escalam com ele), alinha `SCRIPTINFO::base` à direção da
explosão e a explosão vive `SCRIPTINFO::end` quadros; o script cria as entidades do fogo e da fumaça e
uma luz. Sem o script (hash não carregado) a explosão dura 15 ticks.

Divergências: `psiDeleteShard` libera a cadeia na hora (o original põe na lista de liberação do quadro
o endereço do campo, não a cadeia, e liberaria o meio de um bloco já livre); `psiCreateBreakableData`
para nos códigos de DMA/VIF que o original não sabe pular (laço infinito) e não grava além do que a
caixa reserva; `Break_DoBreak` zera o acerto montado na pilha; o vértice novo de `ShatterPolyRecurse`
parte de uma cópia do vértice 0; divisões por zero dão `±FLT_MAX` como no EE; ponteiros nulos que o
original leria são testados e contados (`g_break_guards`, `g_shard_guards`, `g_explode_guards`: zero
nos dados).

Nos dados (dificuldade 2): 439 quebráveis (255 de vidro, 1358 triângulos), 189 destrutíveis e 64 zonas
que machucam. `nf_loader_check` explode uma granada de mão (raio 6, dano 75, de `weapon_data`) no
primeiro quebrável e 2 m à frente do jogador em cada fase e roda 2 s de jogo (`destruicao:`).

### Emissores e partículas (portado, `src/engine/emitter.cpp`)

As definições de emissor vêm do bloco 0x30 do item de efeitos de cada fase (`0100002e`, 18
definições, hashcodes 0x0c00000x; formato em `docs/formats.md`). `Emitter_LoadDefs` copia cada uma
para o heap (`EMITTERDEF_tag`, 0x40 bytes e as chaves de cor `KEYED_CLR`) e a registra no hashtable;
a textura é o `pstex_tag` do item 0x03 (as texturas do item já foram criadas, o bloco 0x30 vem no
fim) e o modelo, se houver, o `celglist_tag` do item 0x02. Na carga as cores das chaves passam para
a escala do GS (r, g e b pela metade; o alfa fica, 255 = 2.0).

`Emitter_Create` faz um objeto do tipo 0x32 (mode 0x104, flags 0x21: lista transparente) com os
dados (`EMITTER_tag`, 0x50 bytes) e, para cada partícula, velocidade, posição, idade, vida e cor. A
direção dada é o eixo y local; a esfera do objeto é o alcance máximo (velocidade máxima vezes vida
máxima). Quem cria: `Effect_Create` (faíscas do material, rajada), `Emitter_CreatePlist`
(posicionamentos 0xf2: definição, duração em ticks, canais de liga/desliga e de "partícula viva",
pisca em ticks; a matriz vem do quatérnio do posicionamento), e objetos ainda não portados (GT,
SpaceLaser, Lock, rastros de bala).

`Emitter_Update`, a cada tick: os canais ligam ou desligam a emissão, o pisca alterna (emite metade
do período, para o período inteiro), a duração acaba (vira rajada: partícula que já viveu não
renasce). Cada partícula viva pega a cor do instante (`ColourGetValue`: interpolação linear entre as
chaves pela fração da vida), anda `vel * REC_FRAME_RATE`, cai (`gravity * 9.8 m/s²`) e envelhece; as
mortas ficam com alfa 0. Nascem até `rate` por tick, na posição do emissor, com a vida, a velocidade
e a direção (ângulo com o eixo e azimute) sorteadas por `Rand_FRandHalf`. Numa rajada, quando todas
morrem, o emissor conta três ticks e pede a remoção. `Emitter_Draw` (chamado por `View_DrawObjList`
para objetos com mode 0x100) desenha a lista de sprites com `psiDrawParticleList` (aditiva se a
definição não tem flags, transparente senão) ou, se a definição tem modelo, um modelo por partícula
viva com a cor dela no tweak e a rotação do emissor, que cresce a cada modelo desenhado.

`psiDrawParticleList` (camada de plataforma, `src/platform/psi_draw.cpp`): no PS2 monta a cadeia de
`CreatePointerSpriteListColour` (textura, constantes e lotes de até 28 partículas) e o microprograma
da VU1 (entrada `MSCAL 4` em 0x46e com lista de cores, `MSCAL 6` em 0x42b sem) projeta cada centro e
desenha um SPRITE quadrado de meia largura `Z * q` em pixels (`Z` do GS ≈ `Ps2Near2 * Ps2Far1 / z`,
`q = 4 * size / PS2ParticleSize`), descartando o sprite cujo centro sai do dobro da tela; a textura
fica girada 180 graus e só `count & ~3` partículas são desenhadas. No PC o registro guarda posições,
cores e um tamanho no mundo equivalente (`psi_particle_half_size`: cerca de `0.432 * size` m, o
tamanho que o sprite tem no PS2 a 60 graus) e o renderizador monta os quadrados.

Nos dados: 18 definições por fase; `Emitter_CreatePlist` cria 1 emissor em 07000002, 35 em 07000014
(ligados por canais), 16 em 07000015 (piscando) e 17 em 07000046 (uma fileira com todas as
definições, perto do início do jogador); `nf_loader_check` mostra `emissores=N defs=M` e, depois
de 2 s de jogo, `emissores_vivos` e `sprites=listas/partículas` do último quadro.

Divergências: `ColourGetValue` repete a última chave onde o original leria uma além do fim da tabela
(quando `t + 0.001` alcança a última chave); a w das posições não recebe o lixo da pilha que o
original soma a cada tick; canais além de 255 valem 0.

### Luzes (portado, `src/engine/lights.cpp`)

Cada objeto tem uma luz ambiente, a cor da sala onde está (`Lights_CalcAmbientLight`, que a
aproxima 10% por tick), e as luzes pontuais vivem numa lista dupla (`LightList`, `light_tag` de
0x50 bytes): as fixas da fase (o bloco de "radiadores" do mapa, `Light_SetAmbientRadiators`, e os
posicionamentos do tipo 0x4e, com canal de switch e pisca-pisca), e as dinâmicas, com vida em ticks
(o clarão de cada tiro dura um tick). `Light_Update`, uma vez por tick, tira as
que acabaram, liga as de canal pelo switch e faz piscar as que piscam (acesas nos quadros da máscara
por um tempo, apagadas por um intervalo sorteado, com som ao acender).

Antes de desenhar um objeto, o jogo escolhe as luzes que o alcançam (`Lights_CalcClosestLights`:
distância até o centro da esfera menor que o raio da esfera mais o da luz), da mais próxima para a
mais distante em relação ao raio de cada uma. O tipo da luz filtra: as fixas da fase (tipo 0) só
iluminam objetos com a flag 0x80, as do tipo 1 não iluminam os com 0x100, as
do tipo 2 são lanternas presas a um objeto. As duas primeiras vão à VU1 (`psiLight_SetLights`: a
posição e a cor vezes a intensidade, com 1/raio²) e a cor do objeto (o tweak) é o termo ambiente.

Halos (`Corona_Create`) e lâmpadas que falham (`Flicker_*`) são objetos com o modelo da luz; o
modelo marcador dos posicionamentos 0x4e não é desenhado.

### Esmaecimentos, rastro e tela de carga (portados, `src/platform/psi_draw.cpp`, `psi_loading.cpp`, `src/engine/resetmap.cpp`)

**Esmaecimentos.** `Camera_SetFade(câmera, ticks, 0xRRGGBBAA, trava)` anima `viewer_tag::fade` de 0 até o alfa pedido
(com tempo negativo, do cheio até 0) e `Camera_UpdateGlbVars` anda o estado (`fade_state`: 1 em andamento, 2 no último
quadro, 0 parado; com a trava, outro pedido não começa enquanto este anda). Depois dos sprites de cada câmera, o
`Game_Draw` chama `psiFadeView(r, g, b, a)` para a câmera que esmaece: `AddDMAChainSprite(CreateTotalScreenSprite(...))`,
um sprite sem textura da tela toda, ALPHA_1 0x44 (`(Cs - Cd) x As + Cd`, com As na escala do GS: 0x80 já cobre tudo) e
TEST_1 0x20002 (sem teste de Z), no fim da cadeia do quadro. Como as câmeras 4 a 9 desenham depois das 0 a 3, o
esmaecimento da câmera 0 fica embaixo do HUD (câmera 5) e o da câmera 6 cobre tudo. Quem pede:

| Câmera | Quem | O quê |
|--------|------|-------|
| 0 | `Player_Init` | volta do preto em 45 ticks em todo começo de fase (e em todo recomeço) |
| 0 | `Script_Update` | volta do preto em 22 ticks depois de uma cena pulada ou esmaecida |
| 0 | `Player_Update` (`Player_SetFlashBang`) | o clarão branco da granada de luz e do tremor (`Apocalypse_*`): alfa `BLData::blur`, cheio na primeira metade de `fade_time` e voltando na segunda |
| 4 | `Script_Update` | 20 quadros antes do fim das cenas com câmera (flag 0x40, menos as de morte e quatro outras) começa o preto, em 40 ticks |
| 4 | `Script_FFwd`, `Script_FadeStart` | o preto do pulo (até 45 ticks) e o comando 19 dos scripts (a cena clareia) |
| 6 | `GameFlow_PushState` (pedidos 7 e 0xD) | o preto antes de recomeçar ou sair da missão (80 ticks), do fim da missão (60, cor `FadeClr`), das NIS carregáveis (20) e dos gatilhos de troca de fase (30 ou 300) |

No PC, `psiFadeView` grava um `PsiDrawCall` com `fade = 1` e a cor na ordem dos desenhos, sem dono (fora da
identidade da interpolação); `RuntimeGfx::add_fade` faz dele um quadrado da janela inteira (`DrawItem::fade` = a
câmera; a textura `kQuadNoTexture` é um texel 0x80808080, para o MODULATE do shader dar a cor do vértice, como o GS sem
textura). Os menus Modern não o escondem (`hidden_2d`). A interpolação entre ticks mistura o alfa quando os dois
registros têm um esmaecimento na mesma posição da lista; o caminho Direct3D 9 do Remix desenha o mesmo quadrado.

**O rastro (`ScreenBlur`).** O `Game_Draw` chama `psiSetScreenBlur(GlobalBlur)` (`ScreenBlur = 0x80 - blur / 4`) e
baixa o `GlobalBlur` 16; o `psiPreDraw` seguinte copia o quadro desenhado para o buffer de exibição
(`CreateSqueezeFrame`) com ALPHA_1 FIX = `ScreenBlur`: tela = tela + (novo - tela) x ScreenBlur / 128. Abaixo de 0x80, o
quadro anterior fica um pouco: um rastro. Pedem o clarão (`BLData::blur`, até 255: o novo pesa 0x41 / 0x80), a troca de
zoom (0x7f, um quadro) e a câmera debaixo d'água (`Camera_Shear`, 0x91 a cada tick: o novo pesa 0x5c / 0x80, 72%). A
mesma cópia passa pela cor `SqueezeColourIn` (0x91: o PS2 clareia a imagem 13%), que o port não aplica (a confirmar
no PCSX2 ou no aparelho). No PC, `PsiFrame::screen_blur` guarda o valor do `psiPreDraw` e o `main` chama
`Renderer::screen_blur(keep)` depois do `render`: keep = (1 - ScreenBlur / 128) elevado aos VBlanks do quadro de vídeo
(o mesmo rastro por segundo a 60, 144 ou 240 Hz), o quadro anterior guardado é misturado por cima com esse peso e o
resultado vira o novo anterior. Com keep 0 nada é desenhado nem copiado; os menus Modern zeram o rastro.

**A tela de carga.** O passo 3 do `ResetMap_Load` (antes de `ResetMap_GameInit` e da carga do pacote da fase) chama
`ResetMap_GenLoadScreen`, salvo no menu principal: os sprites da lista 99 com a imagem da fase (`LoadingGfx.266`,
512 x 448, a textura que `ResetMap_LevelCode2Img` escolhe entre as do pacote 07000500: 0x30000d0 a 0x30000d7, 0x300016e
no multijogador), o rótulo 0x3c1 ("LOADING"), o texto da missão (com "OBJECTIVE") e uma dica ("HINT"): a da fase sorteada (`Rand_Random`, das
tabelas de cada fase no `.rodata`) e, num segundo sorteio par ou sem dica da fase, uma das 43 genéricas (na 0700001b,
por um descuido do original, sempre a genérica). Desenha quatro quadros na câmera 10 (os dois buffers ficam com a tela;
o primeiro guarda a textura das bolinhas, 0x3000175, `CacheLoadingBlobs`) e chama `psiStartLoadingScreen`. O `Mem_Init`
seguinte (`psiAllocateLoadingBlobs`) monta sete cadeias de seis quadrados de 16 x 16 (x = 55 + 16 i, y 309 a 325; os n
primeiros acesos, 0x8029a0ee, os outros 0x800c315c) e a `VBlankInt` manda uma delas direto para a tela a cada VBlank,
trocando de cadeia a cada 11 VBlanks (de 0 a 6 e de volta). `psiEndLoadingScreen`, no fim da carga, a desliga. Não há
tempo mínimo: a tela fica o tempo da carga (segundos de DVD no PS2; aqui 0,2 a 0,7 s).

No PC a carga cabe num tick e o laço não desenha durante ela; o gancho `g_psi_loading_hook` (`Wipe` no `PS2WipeVram`
do começo da carga, `Screen` no `psiStartLoadingScreen`, `Blob` quando a cadeia muda, `End`) deixa o `main`
apresentar o preto, a tela (uma cópia guardada, porque o reinício seguinte apaga as texturas dela) e as bolinhas. As
bolinhas andam pelo relógio de parede: `psi_loading_pump`, chamado pelo laço do carregador, roda um VBlank da
`VBlankInt` por sexagésimo de segundo passado. Nas capturas e no `--bench` fora da tela nada é apresentado.

### Chuva, neve, raios e nuvens (portados, `src/engine/weather.cpp`)

As gotas são duas tabelas globais (`DropVector[4096]`: posição, com o fim em w; `DropState[4096]`: 1 viva). O tipo vem
do ambiente da sala (`Env_Create`, parâmetro 0; `Env_Reset`, no `ResetMap_GameInit`, põe 0xff, nenhum, e limpa o céu) e
`InitDrops` prepara as 32 velocidades de `RandTable1` (96 sorteios a cada carga):

| Tipo | O quê | Gotas | Desenho |
|------|-------|-------|---------|
| 0 | neve (07000005 a 08) | 4096 | uma lista de partículas (textura 0x3000045, tamanho `PS2SnowSize` da fase, cor 0x3f3f3f7f) |
| 1 | chuva (07000001 a 04, 09 a 0b, 41) | 128 nas 07000009-0b, 64 nas outras | um modelo por gota viva (0x2000290; em Paris também 0x2000039 virado para a câmera, tweak 0x24) |
| 2 | bolhas que sobem (07000014) | 2048 | uma lista de partículas (textura 0x3000087) |

`Env_Update` roda a cada tick (o `DrawSnow` do `.sdata` é 1): o vento das salas e, com a câmera em uso (a 4 numa cena,
senão a 0) vendo o céu, `UpdateDrops`. Um cubo de 30 m em volta da câmera (7,5 m à frente do jogador fora das cenas) é
recortado contra as caixas de chuva (posicionamento 0x32, `RainBox_Create`: objetos do tipo 1 fora das salas, ligados por
`obj_tag::owner` a partir de `RainBoxRoot`, com a caixa do modelo); até 21 gotas novas por tick nascem acima da câmera,
só dentro de uma caixa ativa, com o chão dela como fim; as vivas andam pelo vento mais uma das 32 velocidades de
`RandTable2` e morrem ao passar do fim. Sem caixa ativa, `Added = 999` e nada é desenhado. `DrawDrops` vem no
`Game_Draw` depois dos estilhaços (câmeras 0 a 3 e 4); o tipo 1 não é desenhado com a câmera 0 debaixo da água da sala.

**Raios** (posicionamento 0x2c, objeto 0x33 que atravessa salas): dois com o mesmo parâmetro 1 formam um par; o
segundo cria seis segmentos e usa o primeiro como o oitavo. Os oito (modelo 0x200032c, esticados de um ao outro por
`PositionBeam`) só aparecem enquanto o céu mostra um relâmpago com repique (`SuperHappyLightningFlag`, `View_DrawSky`),
e a cada repique (`Reflicker`) `FractalThing` sorteia de novo os pontos do meio. Na 07000001 o céu não tem esse
relâmpago e os raios nunca aparecem no jogo; `--force-lightning` (depuração) liga o relâmpago para vê-los.

**Nuvens** (posicionamento 0xd4, objeto 0x24): não são desenhadas como objeto, e sim como um elemento do céu
(`View_AddSkyObj` com o objeto, camada 1) que anda com o jogador e desliza pelos parâmetros 1 e 2 (x 0,001 por tick),
dando a volta no parâmetro 3, à altura do parâmetro 0 acima da câmera.

### Enfeites (portados, `src/engine/scenery.cpp`)

| Posicionamento | Objeto | O quê |
|----------------|--------|-------|
| 0x21 `Ripples_Create` | 0x34 | marolas: a cada tick, com chance `FRAME_RATE_MUL` / parâmetro 1, um anel de água (modelo 0x2000129 por `Gas_Init`, de 5-10 cm ao dobro em 42 ticks) a até parâmetro 0 metros |
| 0x2b `LeafGen_Create` | 0x31 | folhas: com chance 1 em `FRAME_RATE_INT / 2`, uma folha (objeto 0x2e, modelo 0x20002ca) na esfera do gerador, levada na direção dele, caindo em zigue-zague; vive 256 ticks |
| 0xf3 `Env_Tree_Create` | 0x3f | árvores animadas: a pele do parâmetro 0, o script do parâmetro 1 (0x600008c vira 0x600008d com metade da chance) num quadro sorteado, em laço (sem laço na 07000043) |
| 0x71 `rotor_init` | 7 | hélices e ventiladores: giram a rotação do posicionamento por tick enquanto o canal do parâmetro 1 está desligado |
| 0xd5 `OneSided_Create` | 0x4c | paredes de um lado: escondidas quando a câmera 0 está atrás delas |

O bafo (`Env_Breath`, do jogador e dos personagens, a cada décimo de segundo no primeiro terço de cada 2 s) é uma
nuvem branca da boca nas fases com neve (tipo 0) fora das salas cobertas; nas outras só aparece no modo de visão 2
(infravermelho). As partículas das lâmpadas que falham (`Particles_Create`, de `Flicker_Update`) chamam funções da
plataforma que no PS2 são vazias (`psiCreateParticles`, `psiParticleUpdate`): o objeto não mostra nada e some.

Todos sorteiam com o `Rand` como no PS2 (as gotas a cada carga e a cada tick, as marolas, as folhas, as árvores, a dica
da tela de carga), então resultados que dependem do `Rand` (capturas, as provas do `nf_loader_check`) mudam em relação
aos de antes deste porte; `--no-env` (depuração, no `nightfire.exe` e no `nf_loader_check`) deixa o tempo e os enfeites
provisórios e pula a tela de carga, e devolve as imagens e as provas de antes.

### Personagens da IA (núcleo portado, `src/engine/drone*.cpp`)

Inimigos, civis e reféns são "drones": objetos do tipo 2 com um `Drone_tag` de 0xd20 bytes, numa
lista em `NPCGlobals`. O módulo inteiro (NDrone2, Drone, DroneAnim, DroneFunc, DroneVision,
DroneWeap, DroneMove, AINetwork, BOT) tem cerca de 730 funções, mais que tudo o que veio antes.

Criação e carga. O posicionamento 0x0f chama `Drone_Create`, que só cria na dificuldade mínima
pedida (parâmetro 2) e guarda os 33 parâmetros no fim do `Drone_tag` (`DIVars_tag`, +0xc00). Depois
da carga, `Drone_PostLoad_Init` monta os grupos e chama `NDrone2_DefaultInit` para cada um: a pele
decide a classe e a subclasse (a subclasse escolhe as animações), o parâmetro 9 a arma, o 11 a
aparência, o 14 os alcances, o 5 o "modo". Modos abaixo de 0x24 vêm de `DroneModeSettings` (tipo,
lado, alerta, uma função de inicialização que liga propriedades); os outros, de uma estrutura de
comportamento nos parâmetros 15 em diante (`behaviour_util_get`: propriedades em bits, tipos normal,
de ataque e amigo, e às vezes as estatísticas do tipo). O "tipo" (DTYPE, `DroneTypeSettings`) dá o
estado inicial e o de ataque; o capitão (pela dificuldade) troca a pele e dobra vida e dano. Por fim
a animação de parado (DAnim 0x11a) e o personagem é posto no chão (`DroneInit_Collision`).

Animação. Uma DAnim (`Drone_AnimInfo`, 436) tem um script por subclasse (`Drone_AnimTables`), flags
(repetir, giro ao terminar, andar sem animar) e a DAnim seguinte. A IA pede "chamadas" (andar,
mirar...); cada estado de animação (`DroneAnimStates`) tem uma lista que traduz a chamada em DAnim
(`DroneAnim_GetDAnimForAnimState`). `DroneAnim_SetHTAnim` toca o script, misturando com o anterior;
ao terminar a chamada, o personagem recebe o estado e a mensagem combinados
(`DroneAnim_CallFullyComplete`). `DroneAnim_PreTransCallback` decide quanto a raiz anda no mundo.

Navegação (`src/engine/ainetwork.cpp`, `aipath.cpp`). O bloco 0x05 de cada fase traz caminhos
(grafos de nós e ligações: a rede de navegação, patrulhas e rotas de missão) e limites (segmentos
que os personagens não cruzam: bordas, janelas, portas). Na carga cada nó é pendurado na sala onde
está; depois, `Drone_PostLoad_Init` acha as portas e os pontos de chute da rede, marca as ligações
que cruzam limites e calcula, para cada cobertura e ponto de IA, a distância de cada nó até ele
(emissores: um Dijkstra). Uma rota (`AIRoute_tag`, duas por personagem) é um A* com custo
comprimento x (usos + 1), para espalhar os personagens, e é seguida aos poucos (`LinkCreep_*`: passos
de 0,4 m, testando os limites a cada um). `NDrone2_MoveTest*` decide se se anda em linha reta entre
dois pontos (distância, degrau, inclinação, limites).

Máquina de estados. Cada personagem tem uma (`StateMachineInfo_tag`, +0x108) e conversa por
mensagens (`MsgObject`: código, estado exigido, origem, destino ou todos, hora de entrega, dado).
`Drone_SM_RouteMsg` entrega já ou guarda numa lista ordenada (`Drone_SM_SendDelayedMsgs`); a função do
estado (`NDrone2_StateFuncs`, 250 estados) trata ou passa ao estado 0 (`NDrone2_DSTATE_Global`). Uma
troca de estado manda "sair" (2) ao estado velho e "entrar" (1) ao novo. Por tick, `Drone_InitComms`
(antes dos objetos) faz a visão e os oponentes de todos, a música de combate, os canais de alarme e as
mensagens atrasadas; `Drone_Control` (por personagem) mede distâncias, entrega os temporizadores,
manda o tick (3) à máquina, move, atira e anima.

Estados portados: o de partida (`Global`), esperar canal (`WaitSwitch`) e desligado, parado e em
alerta, patrulha (`InitPatrol`/`Patrol`: o caminho de patrulha sob os pés vira `route2`, seguida por
`NDrone2_FollowRoute`, que deixa o próximo ponto em `move_target`), civis (parado, patrulha, missão,
espera, com medo de quem está armado), convidadas da festa, aliado que guia (início), correr até um
ponto de IA e os de morte (animação pela posição do tiro, corpo que some depois do tempo dos
corpos, alerta aos outros). Quase todo inimigo começa esperando um canal (parâmetro 3) ligado por um
gatilho: sem gatilhos, a fase fica vazia.

Visão (`src/engine/drone_vision.cpp`). Uma vez por tick, `Drone_InitComms` monta os pontos de
atenção (projéteis e fumaça que a IA evita), atualiza o oponente de cada lado (posição, distância,
ângulos, o fator dos volumes de IA) e roda `DroneVision_ProcessDroneSight`. Quem já sabe algo alerta
quem ainda não sabe se o vê (até 5 pares por tick). Cada personagem calcula quanto vê o oponente:
0,7 na distância alcance x (1 + 2 x alerta), caindo com o ângulo fora do campo de visão, vezes os
volumes de IA. A linha de visão é testada em rodízio, um osso do oponente por tick
(`Drone_View_Bones`), parando no primeiro teste caro. Passado o tempo de reação, o oponente é visto:
bit 4, a última posição vista e, na primeira vez, a mensagem 0xf. `NDrone2_FindOpponent` escolhe o
oponente: o jogador para inimigos e civis, um inimigo visível para aliados. Os alertas difundidos
(`NPCGlobals.alert`, `NDrone2_DroneAlertToObject/Position`) e os sons ouvidos chegam aos estados como
mensagens, que `DroneVision_EnemyAlerts` trata.

Corpo (`src/engine/drone_move.cpp`, `drone_damage.cpp`). A animação anda a raiz; `NDrone2_Move` só
gira o personagem para o alvo do movimento (`Drone_tag::move_target`, o ponto seguinte da rota posto
por `NDrone2_FollowRoute`; o giro buscado fica em `move_angles.y`), à taxa de +0x4a0 por tick.
`NDrone2_Steer` prevê, para os outros personagens a até 3 m e para o jogador, quando as esferas (raio
0,404; 0,475 o jogador) se encostariam andando em linha reta (`SphereSphereSweep`) e mistura no giro
um desvio para o lado oposto, mais forte quanto mais cedo o contato; guarda também o mais perto, de
que `DroneMove_NoBunching` diz o lado. `DroneMove_SetBoundryFlags` testa meio metro em cada direção
com `NDrone2_MoveTest` para a animação não atravessar limites, e os tiros empurram o personagem
(`push_offset`, até 0,175 m, reduzido a 95% por tick). Colisão: `NDrone2_Collision` monta uma cápsula
de raio 0,4 do quadril até um raio acima dos pés (ao longo do corpo, se morto); depois do
`Collide_Update`, `Drone_CollisionHandler` acha o chão com um raio (`Drone_FeetOnPoint`, que aceita
outro personagem como chão, como no original), aplica o empurrão e cada acerto, acelera a queda
(`Drone_tag::fall`, `WldGravity`) fora do chão e, no chão, leva o quadril à altura `root_y` em 1/8 por
tick. Raios e esferas acertam as caixas das juntas (`AnimGetCollData`, tabelas do ELF; o osso atingido
é o lugar do acerto). Olhar: `NDrone2_DoTracking` mira no oponente quando a DAnim permite
(`NDrone2_AimTest`); senão a cabeça segue o oponente ou o personagem mais perto na sala. Dano: um
projétil de quem pode ferir o personagem (lados opostos; o jogador sempre) solta fumaça, empurra e
manda a mensagem 8 (6 soco, 7 choque, 0x19 dardo) com o acerto; o estado chama
`DroneFunc_HandleImpact`, que leva a `NDrone2_BulletImpact/PunchImpact/ExplosiveImpact`: alerta
total, `NDrone2_HitDamage` (cabeça x100, pernas x0,75, proteções, dificuldade: fácil x2, normal x1,5,
difícil x0,5, valores do ELF), aviso de ferido aos outros (mensagem 0x12, pelo mesmo
`NPCGlobals.alert` dos alertas da visão), fala de dor, animação do lugar atingido (no máximo uma a
cada 2 s) e, sem vida, o estado de morte (0x44; 0x45 explosão; 0x92 sem gravidade; 0xbf tipo 0x1d).
Só os estados portados tratam a mensagem 8; nos outros, o tiro acerta e empurra, mas a vida não cai.

Armas (`src/engine/drone_weapon.cpp`). Um estado de combate pede o tiro com `DroneWeap_Fire`
(`Drone_tag::firing`; com `one_burst`, uma rajada só, que as trocas para os estados de morte também
pedem) e, a cada tick, `NDrone2_ControlSTANDARD` chama `DroneWeap_HandleFiring`: a mira fica no
oponente mais o osso visado (`target_pos`), `DroneWeap_DoOpponentTargetting` nota quando o jogador
começa a andar ou para, e `DroneWeap_DoFiring` atira na hora marcada se o personagem viu o oponente
nos últimos 16 ticks e `DroneWeap_Ready2Fire` concorda (oponente vivo; animação de mirar, `kind` 4 ou
2 sem mistura; com a mira da animação, o cano a menos de 20 graus do alvo). A rajada tem o tamanho do
terceiro modo de disparo da arma na mão (`weapon_data` +0x2c; sorteado em algumas armas; encolhido
pela agressividade de `stats[1]`), uma bala a cada `fire_delay` ticks e, entre rajadas, uma pausa de
15 a 60 ticks (`DroneWeap_BurstDelay`: 45 vezes o fator de `stats[1]` e um sorteio; a mínima perto,
a máxima longe). Cada disparo (`DroneWeap_FireWeapon`) decide se acerta (`DroneWeap_DoBulletAccuracy`:
chance 100 - 5 x precisão, vezes dificuldade e movimento do alvo; só depois de ver o alvo por 2 s, e
sempre a menos de 3 m; errando, um desvio de até 1,5 m que oscila com o tempo), acende uma luz e o
clarão na boca da arma, cria os projéteis com o mesmo `Bullet_init` do jogador (voam, ricocheteiam,
deixam marcas e acertam o jogador e outros personagens; o fogo amigo é filtrado pelo lado no
`NDrone2_DealWithObjHit`), ejeta a cápsula e gasta uma bala do pente. As armas 0x1c/0x1d atiram pela
animação (`DroneAnim_PlayFiringAnim`). Eventos da animação jogam granadas (`DroneWeap_ThrowGrenade`:
a do parâmetro 11, `Drone_tag::look`, contadas em `look_variant`; saem da mão em direção ao alvo, 45
graus para baixo perto e para cima longe, girados em volta do eixo x do mundo como no original), dão
golpes (`DroneWeap_CloseCombatImpact`: projéteis das armas 99 a 0x66 até 1,75 m) e trocam de arma
(`DroneWeap_ChangeWeapon`); ao morrer, o personagem solta a arma (`DroneWeap_DropWeapon`, um pegável
ainda stub). No jogador, o dano de uma bala da IA passa por `Drone_ModPlayerHitDamage` (vezes 2 perto,
vezes o dano do capitão e, na subclasse 0x12, vezes 2 pelas costas), mas os stubs `MP_RegisterBulletHit` e `Player_HandlePain` ainda impedem que a vida dele
caia. Os estados de combate pedem o tiro (abaixo); o `nf_loader_check` também faz cada inimigo armado
atirar uma rajada no jogador (`tiros_ia`).

Combate (`src/engine/drone_combat.cpp`). O estado 0x56 (`Attack`) é a entrada: na primeira vez,
`DroneFunc_FirstAttack` decide a reação à vista do oponente (render-se com a propriedade 0x43,
desafiar ou interrogar com a 0x1a, um movimento de combate com as 0x1b/0x19, a granada com a 0x4a),
liga as flags de ataque (0x2000000/0x4000000), põe o alerta em 1, amplia o alcance de visão (menos em
07000009/0a), avisa os outros (0x16) e grita; quem está desarmado foge (0x12) ou saca a arma
secundária (0x66); atiradores de elite (tipos 2 e 0x19) vão mirar de longe (0x2a); os outros vão ao
combate (0x58) ou, com a propriedade 0x18 e o oponente no alcance, mirar parados (0x67). Os estados de
combate (0x58-0x5f) se revezam por `DroneFunc_CombatState`: anda até o oponente pela rota livre
(`NDrone2_MoveToObject`, até 6 m dele), com a animação escolhida pelo quadrante entre a direção do
passo e a do oponente (`DroneAnim_SetCombatMoveAnim`: correr, andar mirando de frente, de lado ou para
trás, rolar); vendo-o, atira de pé (0x68) ou agachado (0x70); sem vê-lo há 16 ticks, procura (0x5c);
sem rota, 0x5f tenta cobertura, um aliado que o vê ou um ponto mais perto. Atirando, recarrega quando
o pente acaba (`Drone_tag::clip`), recua a 4 m (0x6d), ataca corpo a corpo a 1,5 m (0x72),
procura cobertura (`NDrone2_FindCover`: o nó de cobertura mais perto pela rede, a menos do oponente
que ele, livre e com animação para o lado em que o oponente está; `cover_pos` guarda onde ficar) e às
vezes troca de posição (`NDrone2_ChooseCombatMove`: desvio 0x77/0x78 e rolar 0x79/0x7a quando o
oponente mira nele, andar de lado 0x75/0x76, um passo 0x73/0x74, agachar 0x6f, mirar parado 0x67). Os
tiros, a mira e as granadas são do módulo de armas (acima).

Cobertura: com uma cobertura achada, o personagem corre até ela (0x8c, `RunForCover`) e fica atrás
dela (0x93 a 0x9c): entra sorteando o modo (numa quina, inclinar ou sair, agachado ou de pé, ou usá-la
como baixa; as animações de cada passo vêm das tabelas `CCModes`/`LCModes` do ELF), espera até 2,5 s,
sai para mirar e atirar, volta e às vezes troca de modo; larga a cobertura quando ela deixa de servir
(o oponente mudou de lado) ou depois de três esperas. Atiradores de elite (0x29-0x2c) miram de longe
(alcance de visão 1000 m) e não saem do lugar. Rotas com nós de ação (`NDrone2_ReachedDestNode`, no
`LinkCreep_Handler`) param o personagem para abrir a porta (0x81: `Door_Activate`, empurrando quando
alerta), chutar um objeto (0x82), tocar uma animação (0x83) ou mexer em algo (0x84), e então ele
volta ao estado de antes (`node_return_state`).

Procura e reações. Um alerta (`AlertToPosition`, 0x62) ou um som (`HeardNoise`, 0xa2 a 0xa5: o nível
sobe a cada som, de olhar em volta até girar e atacar) leva o personagem até o lugar pela rota livre
(`GoToGoalPosition`, 99, que procura o oponente no caminho), onde ele procura em volta
(`SearchArea`, 0x65) e volta, calmo, ao lugar de onde saiu. Corpos e rendidos vistos levam ao lugar e ao
ataque. Guardas civis atacam quem veem armado; os de porta (`CivilianDoorGuard`) trabalham em par: os
planos entre os dois (`NDrone2_SetDronePartner`) dizem se o jogador chega, está perto (manda parar)
ou passou (falha a missão e ataca). Quem tem alarme corre até ele (`RunToAlarm`), aperta
(`PressAlarm`: o canal do switch mais perto) e depois ataca ou foge.
Interrogatório (0x39 a 0x3e): um inimigo com a propriedade 0x1a, armado, aponta a arma para o jogador
que vê e segue um roteiro de falas e animações pelo temporizador (próprio em 07000005 e 07000007);
ataca no fim, se o jogador saca a arma (tendo estado desarmado) ou se ele se afasta 4 m além do
ponto mais perto em que esteve (`interrogate_min`). Os que souberam do interrogatório vêm ajudar
(`InterogateAssist`) e esperam perto até ele acabar; civis de 07000009/0a interpelam
(`CivilianChallenge`).

Aliados (0x19 a 0x27). Quem guia (`AllyLead`, 0x1d) segue a rota de missão (`route2`) correndo e para
nos nós de espera; se o jogador fica para trás (mais de 9 m pela rota, `DroneMove_ObjectMissionPathDistance`),
espera por ele (0x20); se ele entra na frente, sai do caminho (0x1e); entre 15 e 25 m do fim da rota,
espera também quando há combate (0x22, pela contagem de atacantes do tick anterior); um inimigo armado
perto o faz fugir ou se esconder (0x1f) e, passado o perigo, `NDrone2_ReFindMissionPath` volta à rota
pelo nó mais perto alcançável. Nós de missão param o aliado até um canal ligar (0x21). Quem segue
(`AllyFollow`, 0x25) corre até 2 m do jogador e espera quando ele está perto. A Kiko (0x19/0x1a) segue
a rota de missão e corre ao alarme quando vê o jogador (menos em 07000016).

Reféns (8 a 0xf). O sequestrador (tipo 4, `HostageKiller`) pega o refém livre mais perto, a menos de
5 m (`NDrone2_SetupHostageKiller`: `unknown_2b0` aponta um para o outro e os dois se olham), aponta a
arma e os dois conversam em turnos (`NDrone2_MonitorTalkToMissionObj`, mensagem 0x1a e a fala de
`NDrone2_TalkToMissionObj` quando a do outro acaba). Visto ou alertado, ele executa o refém (0x58; no
fim da animação o refém recebe a mensagem 0xb e morre) e ataca. O refém (`Hostage`, 0xa) fica rendido
enquanto o sequestrador vive; depois é salvo (0xc: conta em `NPCGlobals.unknown_19c`, liga o canal do
parâmetro 6 e mostra a mensagem em 07000002/03), foge ou fica parado, e se esconde (0xe) enquanto houver
um inimigo armado a 20 m. A emboscada (0x37/0x38) toma a cobertura livre mais perto, a menos de 2 m
(`NDrone2_FindAmbushCover`), ou espera agachada até ver o jogador ou tê-lo a 3 m; o guarda do castelo
(0x36) conversa até o canal do tipo de conversa ligar e então toca o script de entrada.

Ninjas (0xa6 a 0xb3): escolhem o ataque pela distância (longe, correndo e atirando; meia distância,
avançando; perto, atirando e saltando de lado ou para trás; colado, a espada), pulam por cima do
jogador a 5-7 m e levam metade do dano durante as acrobacias. Em 0700001b, o Drake fica parado até o
canal 10 e os astronautas (sem gravidade) seguem a rota de missão e depois flutuam atirando.

Impactos e efeitos: os estados para onde `DroneFunc_HandleImpact` leva (soco 0x53, explosão 0x54,
tiro 0x55, taser 0x4a, granada de atordoar 0x4d, dardo 0x50, fumaça 0x7b) tocam a reação e, nos de
atordoar, deixam o personagem no chão pelo tempo de `DroneFunc_RecoverTime` (pela arma e pela
resistência), até ninguém estar por perto; então ele se levanta e volta a atacar (civis voltam ao que
faziam). Quem se rende (0x3f) larga a arma e desiste (0x41) se o jogador se afasta, desarma ou deixa de
olhar. Também: o motorista de 07000006 (script de entrada e rota de missão), o rapel (desce atirando e
vira um inimigo comum no chão) e a fuga de explosivos (0x86-0x88).

Gatilhos (`src/engine/trigger.cpp`). Objetos do tipo 0x1d que ligam canais de switch: tocados (pelo
jogador, por personagens ou por balas, vistos na lista de acertos do objeto que `Collide_Update`
preenche), usados com E, ou pela lógica dos canais de entrada (E, OU, em sequência, cópia). Também
trocam de fase (`Trigger_Activate` pede a fase do canal e salva o jogador) e tocam filmes. Os de
toque só rodam dentro de `Trigger_Calc`, que os desliga antes e os religa se ainda há alguém dentro.

Geradores (`src/engine/drone_spawner.cpp`). O posicionamento 0xf1 cria um objeto 0x30 com um
`DroneSpawner_tag` (0x3c bytes): o tipo (2 repõe, 3 em levas; menos que 2 não cria), o grupo, quantos
vivos ao mesmo tempo (`max_alive`; 0 ou mais que o grupo: o grupo inteiro), quantas levas (0: 32000),
os canais de começo, parada, fim e remoção e a distância mínima do jogador (inteiro, 0: 10 m); ele se
registra em `NPCGlobals.spawners[grupo]`. Depois da carga, `NDrone2_SetupGroups` chama
`DroneSpawner_Init` para cada grupo com gerador: os personagens do grupo viram modelos (o `DIVars_tag`
de cada um vai para a lista `entries`, 0xd0 bytes por modelo) e saem do mapa (`control_delete_object`),
e `group_counts[grupo]` vira 0xffff. Com o canal de começo já ligado na carga (ou sem canal), a
primeira leva nasce em qualquer lugar. A cada tick, `DroneSpawner_Control` espera o canal de começo,
solta as vagas dos gerados mortos (vida <= 0 ou flags 0x600) e, quando é hora, chama
`DroneSpawner_SpawnDrone`: sorteia um modelo que ainda não gerou nesta leva, longe do jogador e fora da
vista dele (`Drone_PositionVisible`: campo de visão da câmera do jogador, sala visível, linha de
visão), e cria o personagem como `Drone_Create` e `Drone_PostLoad_Init` fariam (`NDrone2_CreateObj`,
`NDrone2_PostLoad_Init`: a mesma inicialização e a mesma máquina de estados dos personagens da fase;
os gerados não entram no grupo). Um por tick e no máximo `max_alive` vivos; cada leva gera
`max_alive` personagens, no máximo um por modelo; o tipo 2 repõe os mortos enquanto houver levas, o
tipo 3 só começa a leva seguinte quando a anterior inteira morreu (em 0700000c/0d com uma fala de
rádio, que nenhum gerador do disco usa). Depois da última leva o gerador some; com canal de fim, só
quando todos os gerados morreram, e então liga o canal. O canal de remoção tira os gerados do mapa (em
07000011-13, 16 e 4a só os que não foram desenhados há pouco, e o gerador espera pelos outros) e o de
parada encerra o gerador. Quando um gerado é apagado, `Drone_Delete` solta a vaga dele
(`DroneSpawner_DroneDelete`).

Peculiaridades mantidas: o sorteio escolhe o n-ésimo modelo livre com n entre 0 e o total de modelos
menos 1, e não gera naquele tick se houver menos livres; a sala guardada de cada modelo é a do objeto
antes de `NDrone2_DefaultInit`, ainda nula, o que desliga a linha de visão de `Drone_PositionVisible`
(na prática só a distância conta); um modelo perto demais do jogador nunca gera e a leva pode não
fechar; `NPCGlobals.groups[grupo]` continua apontando para o primeiro modelo, já apagado.

Nos dados: 79 geradores em 16 fases (07000001, 05, 06, 08, 0a, 0b, 0c, 0d, 11, 12, 13, 14, 15, 16, 1b e
4a), com 270 modelos na dificuldade 2 (07000014: 14 geradores e 49 modelos; 0700000a e 0b começam sem
ninguém no mapa). Sem tocar em nada, em 2 s nascem personagens em 07000005, 0c, 11, 12, 16 e 4a; em
07000001 os capangas aparecem quando a cena de abertura acaba (canal 0xb, perto de 38 s: dois dos cinco
modelos, sorteados). Cenas que desligam a IA (`Drone_EnableAll(0)`) param os geradores também (no
teste forçado do `nf_loader_check`, em 07000008 e 0700001b).

Lacunas fechadas em 12/10/2026 (a lista do estudo do nightfire-cxbx, docs/cxbx.md 3.1, cada uma portada do MIPS):
`NDrone2_NavNodeCache` (0x155cd8), a cada tick do `Drone_InitComms`, põe em `NPCGlobals.player_target` os pés do
jogador, a sala, o objeto e a máscara 0x1000000 e chama `AINetwork_BuildAITarget`, que refaz os nós quando o jogador
andou 2 m: as rotas A* até o jogador passam a existir e o `DSTATE_DroneStuck` vê o `frame` dele avançar;
`DroneFunc_HandleExplosives` (0x1435f0), no fim do `NDrone2_ControlSTANDARD`, manda a mensagem 0x21
(`DroneFunc_ConsiderExplosive`: fugir) para o primeiro ponto de atenção de tipo 2 (granadas e afins, e as nuvens de
fumaça) dentro do raio dele, se o personagem reage a granadas (propriedade 0x3b ou 0x3a), o vê (0,7 ou mais) e tem linha
de visão; o primeiro no raio encerra a busca mesmo sem ser visto, como no original; `Drone_MessageObjVicinity`
(0x13a438) manda uma mensagem a cada personagem a até `radius` m de um objeto, e a do clarão (0x18, a granada de luz, 20
m) só a quem o vê pelo osso 5: os estados `StunGrenade*`; `Occlude_Create/Update` (0x17ea80) fazem a nuvem da granada
de fumaça (tipo 0x2a, modelo 0x2000273, 20 s ou 40 s na classe 0x69), que vira dois pontos de atenção de raio 3 e marca
as ligações da rede com 0x200; `Drone_FellOutMap` (0x13a878) desliga e apaga quem sai de todas as salas; os controles
por tipo (`DroneTypeSettings` +8) da Zoe (0x14b058: o canal 1 liga o 0x9b; com ele ligado e o jogador a mais de 10 m
dela, `DroneFunc_SetMissionFailReason(0xf)`, o canal 99), dos ninjas (os olhos, `NDrone2_CreateNinjaEyes`, ainda stub)
e dos astronautas (o padrão). O parâmetro 13 do posicionamento é o script da morte especial (`Drone_tag::death_script`,
tocado pelo `NDrone2_DSTATE_SpecialDeath_Anim`), não um script de entrada.

Portado: criação, carga, inicialização, modos e tipos, propriedades, a camada de animação, a máquina
de estados, o controle por tick, a rede de navegação, os estados acima, a visão, o corpo (movimento,
colisão, gravidade, olhar, dano), as armas, os gatilhos, os geradores e todos os estados menos os dos
bots;
`PlayScript` (3, `src/engine/drone_script.cpp`) toca o script de animação de entrada do posicionamento
(`Drone_tag::entry_script`) e volta ao estado inicial no fim (os scripts de ataque 06000168/06000169
falam e atacam; 060007ac gira meia volta). Ainda não: bots.

### Script player (portado, `src/engine/script.cpp`, `script_player.cpp`)

As cenas da fase são scripts (itens tipo 7, hash `06xxxxxx`; formato em `docs/formats.md`, "Scripts de
cena"). Na carga, `LoaderProcess` passa cada um a `Script_Load` com os dados: o protótipo
(`SCRIPTDEF_tag`, 0x214 bytes) vai para a tabela de hashcodes e aponta para dentro do arquivo carregado.
Quem toca um script chama `Script_Load` sem dados, que cria uma instância (`SCRIPTINFO`, 0xac0 bytes)
num lugar (`View_RotTransMatrix(rot, pos)`): até 62 streams (`SSTREAM`, 0x28 bytes), cada um com a sua
fila de comandos, o seu tempo e um intervalo de quadros-chave.

A cada tick, `Script_Update` roda `Script_Run` em todos os streams e soma `FRAME_RATE_MUL` aos tempos.
`Script_Run` primeiro põe no lugar o que o stream controla (`Script_Interp`) e depois executa comandos
até um que espere um quadro futuro: criar uma entidade (`Script_EntityStart`/`CreateEntity`: um objeto
simples com o modelo do hash ou, pelo tipo do comando, um quebrável, um interruptor, gatilhos, um volume
de IA, um item, um emissor), tocar um script de animação numa pele (`Script_AnimStart`: o quadro do
script de animação segue o tempo do stream, e os passos deixam marcas), mover a câmera
(`Script_CameraStart`), tocar um som, criar uma luz, um sprite, uma legenda, um sub-script, um fade, ou
um evento (`Script_EventHandler`: pausar, desligar e religar o jogador e a IA, devolver o jogador num
lugar, ligar canais, criar um personagem, quebrar um objeto, trocar de fase, chamar o callback do dono).
`Script_GetInterp` interpola os quadros-chave no quadro inteiro atual: linear (slerp na rotação) ou
Catmull-Rom (`GetSplineWeights`; o original passa o peso de `keys[1]` como fração ao `Spline_Eval3D` e ao
slerp, e a cor das luzes usa o peso do primeiro quadro: peculiaridades mantidas). Um stream pode ter um
pai, cuja posição se soma à dele, e a do script soma no fim (`Concat`). O fim de cada stream é o fim do
script: parado ali, o script termina ou, em laço, volta ao `loop_start`.

A câmera de uma cena é a 4: `Script_CameraStart` guarda o estado de todas as câmeras na pilha de cada
uma (`Camera_PushStates`, `Stack_*`), desliga as câmeras 0 a 6, liga a 4 e, a cada tick,
`Script_UpdateCamera` copia a matriz interpolada para ela, com o campo de visão do quadro-chave vezes a
proporção. `Script_KillStream` devolve o estado das câmeras (`Camera_PopStates`). `ScriptCam` guarda o
hash da cena com câmera; no PC, `Script_ActiveCamera` diz ao `main.cpp` que desenhe pela câmera 4.
Perto do fim de uma cena a câmera 4 escurece (`Camera_SetFade`), e pular a cena (`Script_FFwd`, ação
0x19) faz um fade de até 45 ticks durante o qual o script corre até o fim; depois a câmera 0 volta do preto em 22
ticks. O `SP_Update` lê a ação 0x19 (o Start: segurada, `Input_Action(-1, 0x19, 1)`) enquanto o script player toca
(estado 2), nunca numa cena de morte (`Script_IsDeathNIS`), e o `Script_FFwd` só pula os scripts com o bit 0x10 de
`SCRIPTINFO::state` que ainda não estão pulando. No PC o Start é Enter ou Espaço (os comandos `pause` e
`menu_select`) e Start ou A no controle; como o jogador está desligado na cena, a pausa não abre, e o `Script_FFwd`
limpa as ações (`Input_ClearAllActions`).

O script player (objeto tipo 0x1b; posicionamento 0xd9 ou `SP_Create` de outro objeto) guarda dois
scripts (A e B) e uma máquina de estados: 0 espera o gatilho e carrega, 1 carregado e parado, 2
tocando, 3 tocou (espera para tocar o outro), 4 tocando o outro, 5 acabou (o objeto é apagado), 6 em
laço (o gatilho solta as pausas), 7 carrega no próximo tick. Os gatilhos (`SCRIPTPLAYER_tag::triggers`)
são o canal do player, tiros nas entidades do script (`SP_Hit`, com `Collide_FilterBullets` para ignorar
socos e afins), usar (E numa entidade: `Player_Activate` → `SP_Activate`) e o jogador ou um personagem
encostando nelas; tiros também somam dano até um limite (`SP_GetHitDamage`). Uma pausa do script (evento
3) espera até `SP_UnPause`. Se o script A é de uma seção carregável (`isLoadable`), o player primeiro
empilha o estado 0xd do GameFlow e depois `SP_LoadScript` chama `LoadableLoad`, que lê o pacote da cena
(078, 079, 07A) por cima das animações da fase; `Script_Free` as devolve (`LoadableReload`).

Interruptores (`Switch_*`, objeto 0x39 sem modelo próprio: a alavanca é a entidade do script, que toca em
laço e pausa em cada posição) viram o canal quando usados pela frente com o script parado, e
acompanham o canal quando outro o muda (`Switch_Update`, com volta automática pelo timer). Caixas de
fusíveis (`FuseBox_*`, 0x3c): o script cria os fusíveis e os registra pelo evento 15
(`FuseBox_Register`) em duas listas; quando todos os de uma lista levam tiro, os canais dela ligam.

Na prática: os alarmes de 07000009 baixam a alavanca e ligam o canal 0x7c ao usar; o elevador de
0700004a desce com o jogador; 07000001 começa com a cena NIS de Kiko e Mayhew (35 s, câmera 4,
personagens animados), que devolve o jogador no marcador da cena e liga o canal 0xb (o ataque dos
ninjas). A granada (07000016) explode com o fogo e a luz do script 0x6000052.

### Camada 2D: textos, fontes, mensagens e sprites (portado, `src/engine/txt.cpp`, `font.cpp`, `text.cpp`, `sprite.cpp`, `src/platform/psi_2d.cpp`)

Textos (`Txt_*`, formato em `docs/formats.md`, "Arquivos de texto"): `Txt_LoadLanguage` lê
`<idioma>Txt.dat` para o heap a cada fase (`Txt_LanguageInit` no `ResetMap_GameInit`); o idioma vem do
menu (`P_LANGUAGE_Handler` chama `Txt_SetLanguage(LanguageToLoad)`, 6 = USA no primeiro boot do `main`).
`Txt_BindLabel` dá a string de um rótulo; `Txt_GetStringFromHeap` dá um buffer de 0x168 bytes do
StringHeap (256, em rodízio; `Txt_LockString`/`Txt_UnlockString` contam quem ainda usa um buffer). Só do port: com o
`USATxt.dat`, os rótulos que falam do hardware do PS2 (o cartão de memória, o controle DUALSHOCK®2, o multitap) dão um
texto do PC (`PcText_GameLabel`, `src/engine/pc_text.h`; a lista em `docs/port.md`, "Textos do PC no lugar dos do PS2").

Fontes (`Font_*`, formato em `docs/formats.md`, "Fontes"): três fontes do próprio executável, com a
imagem nas texturas `0x03000005`, `0x03000003` e `0x030000d8` do item `010001d8`. `__Font_DrawText` mede
ou desenha: cada glifo é um `SPRITE_DRAW` (o molde `CharInfo`, com a textura e 1 / tamanho postos por
`Font_ParseFormat`) copiado para a lista da tela (`View_AddSprite`). `Font_DrawText` aplica o alinhamento
e as cópias de efeito (sombra em +1, contorno de quatro cópias em ±1, realce em -1).
`Font_WordWrapString` quebra no último espaço que coube e devolve a linha num buffer do StringHeap.

Sprites (`Sprite_*`): 512 nós de 0x44 bytes (`sprite`: textura ou texto, posição na tela do PS2 de 512 x
448, tamanho, recorte, cor 0xRRGGBBAA, cor da sombra, prioridade, câmera, escala do texto; campos e
flags em `sprite.h`). `Sprite_Create2` monta um a partir de um `SpriteInfo` (imagem por hashcode, ou
texto por rótulo e formato, medido para alinhar). Em cada `Game_Draw`, por câmera que desenha sprites:
`Sprite_BuildList` junta os sprites da câmera (prioridade diferente de -1, alfa diferente de 0) em ordem
decrescente de prioridade, e `View_DrawSprites` desenha (imagens: um `SPRITE_DRAW` por ladrilho, com a
sombra e o espelhamento; textos: `Font_DrawText`). Prioridade >= 50 é fundo, que só as câmeras especiais
desenhariam antes da cena (a contagem de fundo sai um a menos, como no original, e nenhuma câmera chega a
desenhar o fundo antes: `count_a` é sempre 0). `View_3DPoint2Screen` (em `view.cpp`) leva um ponto do
mundo para a tela da câmera (para o HUD).

A plataforma (`psiDrawSprites`, `platform/psi_2d.h`): no PS2 cada `SPRITE_DRAW` vira um SPRITE do GS
(ALPHA_1 0x44, 0x48 com a flag 0x100, 0x42 com 0x200; MODULATE com a cor (r, g, b, a / 2); ST com meio
texel a mais, menos nas texturas de fonte de `FontList`, que usam filtro de ponto; sem teste nem escrita de
Z entre `psiStartSprites` e `psiEndSprites`). No PC vira o registro 2D do quadro, com a origem de cada
elemento (ver `docs/port.md`, "Camada 2D").

Mensagens (`Text_*`): `Text_AddMsg(jogador, prioridade, tipo, texto, dado, tempo)` quebra o texto na
largura do tipo (`TextMsgFormats`) e põe cada linha na fila (`MsgQueue`, `TXT_MSG` de 0x20 bytes, em
ordem de prioridade) com uma parte do tempo proporcional ao tamanho (no mínimo 60 ticks fora das
legendas). Tipos: 1 informação, 2 objetivo, 3 situação da missão, 4 legenda de uma linha (em cena com
câmera), 5 legenda de duas linhas (fora dela), 6 item pego. `Text_UpdateMsg` conta o tempo da mensagem
atual de um jogador e tipo (`FRAME_RATE_MUL` por tick) e passa à próxima. Quem mostra os tipos 1, 2, 3 e 6
é o HUD (`HUD_UpdateStatusPane`, painéis de informação, objetivo, missão e item); as legendas são de
`Text_Update` (o sprite `SubTitleSpr` em (256, 426), com contorno) e `Text_Update2Line` (`SubTitleSpr1` e
`SubTitleSpr2` em y 402 e 422), que o `Game_Run` chama conforme haja cena com câmera. Legendas só entram
com a opção ligada (`DrawInfo[2]`), a não ser forçadas (prioridade 0xff). Os tipos 0, 2 e 3 também vão
para o histórico `ReceivedMsgs` (128, `CirBuf_*`).

De onde vêm as mensagens: portas e gatilhos (`Door_Update`: "Door Unlocked"; `Check_Target` olhando
uma porta a menos de 5 m: "Press Action to open door"; `Trigger_Activate`: "Warning: Not all objectives
completed"), itens (`Pickup_Handler`: "Picked up 20x 9mm rounds"), armas e o jogador; legendas dos filmes
de fundo (o comando de texto 0x1e dos scripts do pacote 07000500, `Script_TextStart`) e das falas
(`Sound_DoSubtitle`, quando o som de uma fala de `Snd2Lbl` começa: Kiko e Mayhew na cena de abertura de
07000001, a partir de uns 11 s).

A legenda de uma fala sai do caminho real do som (veja "Som"): `Sound_ReqestPlaySfx` a decide (som 2D sempre; 3D
numa cena com câmera, para sons sem "só perto" ou com o ouvinte a menos de metade do alcance) e chama
`Sound_DoSubtitle` quando pede o som ao IOP.

A caixa de texto dos menus (`Text_Init(M_TEXTBOX*, ...)`, `TextBox_Update`, `Text_SendMessage`,
`Text_SetupChar`): o teclado da tela para digitar um nome (65 teclas; '-' apaga, '+' fecha). Portada,
mas só os menus a chamariam (eles e os componentes que ela usa continuam stubs).

### HUD e missão (portados, `src/engine/hud.cpp`, `src/engine/mission.cpp`)

`BLData::hud` aponta para um `HUDINFO_tag` (0x2c8 bytes, no heap): o sprite da mira e 22 panes
(`HUDPANE_tag`, 0x20 bytes: a descrição de origem, as palavras extra, os sprites, a função de
atualização, posição e tamanho, quantos sprites, o estado em +0x1a que `HUD_State` lê, ligada em
+0x1c e existe em +0x1f). O byte +0x1a4 que `Player_HandleDeath` lê é o "ligada" da pane 0xc.

| pane | o que mostra | criação / atualização |
|------|--------------|-----------------------|
| 0 | munição: "pente-guardada" (ou %), modo de disparo, nome da arma nova, a pilha de balas, o alvo das armas 0x50/0x51 | `CreateAmmoPane` / `UpdateAmmoPane` |
| 1 | colete (8 segmentos), vida (7), ícone, o 007 girando do momento Bond, ícones de toque e de agachar, as 4 setas da direção do tiro | `CreateHealthPane` / `UpdateHealthPane` |
| 2, 3, 4, 21 | mensagens da missão, dos objetivos, de informação e dos itens (faixa que abre, texto, esmaece) | `Create*StatusPane` / `UpdateStatusPane` |
| 5 | fôlego debaixo d'água | `CreateDefault` / `UpdateAirPane` |
| 6, 18, 19 | lunetas (comum, Ronin, laser) | `CreateShrink` / `UpdateSightPane` |
| 7, 15 | visão noturna e raio X (ruído, varredura, bateria) | `CreateShrink`, `CreateDefault` / `UpdateNightSightPane`, `UpdateXRayPane` |
| 8 | reflexo do sol | `CreateDefault` / `UpdateLensFlarePane` |
| 9 | míssil Redeemer (modelos 3D da mira na câmera 5, helicópteros, combustível) | `CreateRedeemer` / `UpdateRedeemerPane` |
| 10 | carrinho de controle remoto | `CreateShrink` / `UpdateCarPane` |
| 11 | câmera espiã (marca os inimigos com objetos) | `CreateDefault` / `UpdateCameraPane` |
| 12 | cortina de sangue da morte | `CreateShrink` / `UpdateBloodPane` |
| 13, 14 | multijogador: placar e radar | `CreateMPScorePane`, `CreateRadar` / `MPUpdatePane`, `RadarUpdate` |
| 16 | câmera de segurança | `CreateDefault` / `UpdateSecCamPane` |
| 17 | OICW (o "boot" da mira) | `CreateOICWPane` / `UpdateOICWPane` |
| 20 | estação espacial (só em 0700001b: mísseis e alarmes) | `CreateDefault` / `UpdateSpacePane` |

`HUD_Init` (de `Player_Init`) percorre `PaneList` (`MPPaneList` no multijogador): cada pane com
descrição nasce ligada, é criada (os sprites saem das `SpriteInfo` da descrição, deslocados pela
posição da pane, que `HUD_ValidateXY` prende 16 pixels para dentro da tela) e atualizada uma vez;
`HUD_Reset` desliga as especiais (5-7, 9-12, 15-20). A cada tick `HUD_Update` (de `Player_Update`)
atualiza a mira (`HUD_UpdateCrossHair`: a mira na posição da mira suavizada, ou a pane da luneta da
arma quando o jogador mira com uma arma de luneta), a visão noturna (`HUD_MonitorNightSight`: os
óculos 0x5d, a ação 13 alterna nada, noturna e raio X, a bateria em `BLData` +0x920) e cada pane:
ligada, os sprites voltam ao modo da descrição; desligada, escondidos; e a função de atualização
dela. As atualizações só escrevem nos sprites (posição, cor e alfa, texto, modo de desenho), que o
módulo 2D desenha (`View_DrawSprites`).

As tabelas estáticas vêm do ACTION.ELF na partida (`src/game/hud_tables.cpp`): o construtor estático
do HUD roda no interpretador MIPS (monta quatro panes de mensagem no .bss), e as panes alcançáveis
pelas duas listas, as `SpriteInfo` delas, os formatos de fonte, `CrossHair`, `HUDCrossCoords`,
`BulletImg.189`, `TexUV.291`, `MissionData` com os objetivos e o `sp_level` do menu são copiados, com
os ponteiros de código traduzidos por `Hud_ResolveCode`.

Os sprites das panes são os da camada 2D (`sprite` e `SpriteInfo`, criados por `Sprite_Create2`; o
"modo de desenho" do HUD é `sprite::priority`, -1 esconde), desenhados por `Game_Draw` como qualquer
outro (as panes na câmera 5, a mira na do jogador). `HUD_Describe` põe o estado das panes no relatório de `--screenshot`: `hud paineis ... vida H (acesos/7) colete A (acesos/8)
municao C-S sangue F (setas) mira X cortina L/E`.

A missão. `MissionData` (24 fases de 0x28 bytes) liga cada fase à missão (`BaseMap`, o pacote da
primeira fase), à ordem dentro dela e aos objetivos (`OBJECTIVE_tag`, 0x18 bytes: rótulos, o canal
que cumpre o objetivo, o valor inicial dele, o canal que o revela, a flag 2 de "não deixe acontecer"
e o estado: 0 escondido, 1 esperando o canal, 2/3 ativo, 4 cumprido, 5 fracassado). `Mission_Init`
(de `ResetMap_GameInit`, fora do menu) acha a fase e reinicia os objetivos desta fase e das
seguintes; os das anteriores ficam (a missão de várias fases continua). Num reinício frio a missão
começa do zero; sem entrada na `MissionData` o estado é 5 e `PlrStat_OkToUpdate` recusa mudar a
vida (fora do multijogador, o jogador morre ao nascer nessas fases, como no PS2).

`Mission_Update` (em `Game_Run`), a cada tick: com o canal 0x62 (morte) a missão fracassa (estado 3,
vida zerada, 1 s); senão `Mission_MonitorObjectives` anuncia os objetivos (`Text_AddMsg`) quando a
fase deles chega ou o canal que os revela liga, cumpre os comuns quando o canal liga e fracassa os
"não deixe acontecer" quando o canal liga (canal 99); os canais 0x60/0x61 (alarmes) e os de
07000016/0700001b também fracassam a missão com um rótulo de falha; tudo satisfeito liga o canal 100 (peculiaridade
mantida: o "tudo satisfeito" é um AND com o byte cru de cada canal, 0x1828b4 `lbu` e 0x1828c0 `and`, então um canal
com valor par conta como não cumprido).
Canal 99: estado 2, 5 s de mensagem; canal 100: estado 6, 4 s, e `ResetMap_LevelToLoad(LevelToEndTo)`
(o menu 07000048 ou o filme do fim da missão, 073xxxxx, que volta a ele) com um fade. Fracassada, o
original abre o menu de fim de missão (`P_ENDMISSION_Handler`: recomeçar a fase, recomeçar a missão,
sair).

O menu de fim de missão é o do jogo (ver "Menus", abaixo): `Mission_Update` o cria
(`MenuManager_Create(0x80000004, 0x40000042, ...)`) e pausa o jogo. Depois de uma missão cumprida, o pacote do
menu (07000048) mostra o resultado (página 0x40000036) e o "continuar" dele vai ao mapa das missões
(0x4000001c, `P_NFMAP_Handler`), com a missão seguinte liberada (ver "Menu principal").
Na prática: em 07000012 os inimigos acertam o jogador parado a partir de ~6 s (com o ajuste da fase, x 0,6 do dano no
Agent, a vida está em 41, 3/7 no HUD, aos 9 s), ele morre aos ~15,3 s (aos ~8,4 s antes do `ReadTuningVars`), a
cortina desce e, com Enter ou Espaço (a ação 25), a missão fracassa e o menu "Mission Failed" aparece; REPLAY recomeça
a fase com a vida cheia (`--input "none:17 space:0.3 none:2.5 space:0.3" --wait 23`: `recomecos 1`). Em 0700000d, o exemplo de antes, quem atirava
logo no começo eram modelos de geradores, que agora saem do mapa na carga.

### Estatísticas e pontuação (portadas, `src/engine/player_stats.cpp`)

O módulo `PlrStat_*` (0x00198318-0x00199b18) conta o que o jogador faz numa missão e dá a pontuação do resultado.
Os contadores ficam em `PlrMissionStats` (8 jogadores x 0x38 bytes, `PLRMISSIONSTATS`) e `BondMoments` (um por canal
0x65-0x6e); `PlrStat_ResetForMission` os zera no começo da missão (`Mission_Init`, só na primeira fase dela) e
`PlrStat_Init` na partida. Quem registra:

| Contador | Quem chama |
|----------|------------|
| inimigos que viram o jogador | `DroneVision_LogSeenOpponent` (uma vez por personagem) |
| inimigos criados | `NDrone2_DefaultInit` (lado 1; conta sempre, também os modelos dos geradores) |
| abatidos pelo jogador | `DroneFunc_OnInitDeath`, `NDrone2_LogDroneDeath` |
| desacordados, rendidos | `NDrone2_DSTATE_KnockedOut_Anim`, `NDrone2_DSTATE_Surrender_Anim` |
| tiros disparados | `Player_WeaponInitBullet` (armas com `stats & 0xf8`; as torretas também, `GT_Update`, não portado) |
| acertos em inimigos | `NDrone2_HitDamage`, `NDrone2_ExplosiveImpact`, `DroneFunc_HandleImpact` (nunca mais que os disparos) |
| tiro num alvo de cenário | `Destroy_Smash`, `SP_Update` (scripts 0x60007e2/e4-ea): desconta um disparo |
| vida | `Player_SetHealth` |
| momentos Bond | `Trigger_Calc`: o primeiro de cada canal 0x65-0x6e conta e acende o 007 girando no HUD (`BLData::bond_moment` = 200 ticks, pane 1 sprite 16) |
| fichas "007 Bonus" | `Pickup_Handler` |
| tempo (centésimos) | `Player_Init` religa o cronômetro, `Player_Update` soma, `GS_PauseGame` e os eventos de pausa dos scripts de cena o param |

Só contam com a missão em andamento (`Mission_Status` 0 ou 1) ou no multijogador (`PlrStat_OkToUpdate`); o tempo e os
criados contam sempre. O tempo é real, como no PS2 (`psiGetTimeIn100ths`, o relógio de VBlank).

A pontuação. `ScoringTable` (12 missões x 0x3c bytes, `SCORETABLE`) é copiada do ELF na partida (`kTables`), com as
linhas de cada missão (`HT_Level_*_ST`, 9 x 0x14 bytes, uma a cada 0xb8; o ponteiro em +0x18 vira o lugar na cópia) e
`ScoringTableDifMul` (0, 1, 2). Cada missão (o `BaseMap`: 07000001, 07000005, 07000009, 0700000c, 07000011, 07000014,
0700001b e as cinco de direção 09xxxxxx) tem os totais das quatro medalhas, a penalidade por detecção e as linhas
(alvo, pontos, valor, fração, pontos ganhos): 0 momentos Bond, 1 abatidos, 2 desacordados, 3 rendidos, 4 detecções,
5 precisão, 6 vida, 7 tempo, 8 fichas 007. `PlrStat_GetScore(peek)` acha a missão de `Mission_BaseMapHCode`, põe os
valores (a pé, das estatísticas do jogador 0: a precisão é acertos x 100 / disparos, 100 sem disparos; a vida, 0 se
ele morreu; nas de direção, de `NewScoresRef`, que o DRIVING.ELF preencheria) e calcula cada linha com alvo:

- detecções: até o alvo, todos os pontos; acima, perde a penalidade (0,03 se zero) por detecção;
- tempo (t em segundos, no mínimo 1): pontos x alvo / t, sem limite (terminar em metade do tempo dá o dobro; uma
  missão cumprida em segundos, como com `--win`, passa de milhões);
- fichas 007: todos os pontos com o alvo alcançado, senão nada;
- as outras: pontos x min(valor, alvo) / alvo.

Os pontos vão para cima (com tolerância de 1e-5) e a fração é a razão em centésimos, presa em [0, 1]; a soma é
acumulada em float. O bônus é a soma vezes o multiplicador da dificuldade (Operative 0, Agent 1, 00 Agent 2); com
`peek` 0 (o resultado e as estatísticas; a aba SCORE da pausa passa 1) também o total (soma + bônus), o recorde e
`DoneBetter` (o total alcançou o recorde). Fora do estado 6 (missão cumprida) os pontos ficam zerados. A medalha é a
do resultado (`P_NFRESULTS_Handler`): o maior limite que o total alcança (bronze, prata, ouro, platina). O port
calcula com o arredondamento da FPU do EE (para zero): no arredondamento do PC o "teto" com tolerância daria um
ponto a mais em casos de borda.

Tudo isso é memória estática (.data e .bss no PS2, globais no port): sobrevive ao `ResetMap_Load`/`Mem_Init` da troca
de fase e chega ao menu principal (07000048), onde `Mission_Init` não mexe no `BaseMap` nem no estado (6); o resultado
(0x40000036) e as estatísticas (0x40000037) leem a pontuação da missão. `PlrStats_HasGoldMedal` (o bit da medalha 3
da missão no perfil do jogador 0, `Menu_HasMedal`) decide se as 50 fichas "007 Bonus" do disco existem
(`Pickup_Create`, tipo 6). `PlrStats_ResetScoring`/`SetupScoreTable`/`GetScoreTable` são do perfil (o nome do agente
e o salvamento, `P_CNNAME_Handler`, `LS_*`), `GetLevelTotals` dos recordes do menu principal, `Get/SetDrivingData` da
troca com o DRIVING.ELF.

O relatório de `--screenshot` tem a linha `estatisticas tiros acertos abatidos desacordados rendidos vistos gerados
vida momentos fichas tempo parado | missao pontos bonus total recorde medalha` (`PlrStat_Describe`). O
`nf_loader_check` confere a tabela copiada (`pontuação: 12 missões ...`), zera as estatísticas antes de cada fase,
conta os tiros da pistola como `Player_WeaponInitBullet`, anda o relógio de 60 Hz com os ticks de jogo e imprime
`estatisticas=[...]` e `pontuacao_se_cumprida=[...]` (a pontuação no estado 6, com a tabela restaurada depois); no
fim, recarrega as fases com todas as medalhas e conta as fichas 007 (`fichas 007 com a medalha de ouro: ... total=50`).

### Som (portado, `src/engine/sound.cpp`, `src/platform/psi_sound.cpp`; o driver do IOP em `src/platform/sfx_iop.cpp`, `spu2.cpp`, `audio_out.cpp`)

Os formatos, a tabela dos comandos e as regras do driver estão em `docs/formats.md`, "Áudio". O som do jogo tem três
andares, e o port reproduz os três:

1. **A lógica (EE, `Sound_*`, 25 funções):** `Sound_Init` (em `ResetMap_GameInit`: a lista `DynamicSoundList` de 256
   nós de 0x60 bytes e `SFXInitialise`, uma vez por sessão), `Sound_Ready` (no `LoaderLoad`, quando o pacote da fase
   chega: `SFXReset` e `SFXLoadSoundBank` do banco da fase, um `switch` por fase; o menu principal alterna 0xc e 0x38
   a cada carga), `Sound_LoadMapSounds` (bloco 0x28), `Sound_Play`/`Sound_Play3D` (um nó no estado 0; o "handle" é o
   ponteiro do nó), `Sound_UpdateListeners` (de `Game_Run`, a cada tick: o ouvinte é a câmera de cada jogador, a 4
   durante uma cena; o ambiente da sala dela; os sons de ambiente do mapa; `Sound_UpdateSounds` e `SFXUpdate`),
   `Sound_ReqestPlaySfx` (a legenda das falas e a recusa dos sons "3D de verdade" longe demais), `Sound_Stop*`,
   `Sound_SetPosition/SetVolume`, o quanto a IA ouve (`Sound_Alertness`, `Sound_SetAlertness/ModAlertness/
   ZeroAlertness`: antes um stub que devolvia 0, agora a IA ouve os tiros e os passos pelo `SFXOutputData` +0x0c, o
   que muda o comportamento dela no `nf_loader_check`), os gatilhos do mapa (`SoundTrigger_Create/Update`,
   posicionamento 0xf9, tipo 0x40: um som 2D ou 3D ligado e parado por canais, de uma vez ou exclusivo; e
   `MusicTrigger_Create/Update`, 0xfb, tipo 0x44: um evento de música mandado por 30 ticks quando o canal liga; ver
   `sound.h`) e a música (`engine/music.cpp`, 26 funções: `Music_Event`, `Get_Music_Event`, `UpdateMusicalEvents`,
   `PickRandomStealth/Combat` e as 21 `UpdateMFX_*`, uma por fase; ver abaixo). As falas da IA passam por
   `NDrone2_SpeechSFX` (`Drone_SfxInfo`): as de patrulha, ataque, dor, visão etc. já estavam portadas e agora soam;
   com o som vieram o grito de morte por classe (`NDrone2_DeathTalk`), a conversa dos guardas de 07000008 com o
   jogador (`NDrone2_HandleTalking`), os alto-falantes de 0700000c/0d (`NDrone2_PowerstationTalk`, a tabela
   `PowerSfx` do ELF), o Mayhew nos nós de rota (`Drone_MayhewTalk`) e o que os convidados da festa de 07000007 dizem
   ao jogador (`NDrone2_BondTalk` e `DroneFunc_DoBondTalk`, ver abaixo). A maior parte das falas da IA tem um script de
   rosto (`Drone_SfxInfo` +4, um 06xxxxxx) em que o som é o comando 1: `NDrone2_SpeechSFX` não toca o som, pendura o
   script na lista de animação do personagem (`AnimFacialAppend`, a marca 0xe0000002 e o fim 3) e o som sai quando o
   script passa pelo quadro dele; sem `AnimFacialAppend` (um stub que devolvia nulo até aqui) essas falas ficavam
   mudas. O mesmo script move a boca (a sequência de rosto dele, ver "Rostos").

**A conversa com o jogador em 07000007 (`NDrone2_BondTalk`).** Os estados `Civilian`, `CivilianMission`,
`CivilianMissionWait`, `PartyGirl` e `CivilianGuard` a chamam a cada tick em que ninguém armado está perto. Ela só age
em 07000007, com o personagem desenhado há pouco (`obj_tag::unknown_fc`), desarmado, a até 3 m do oponente (o
jogador: `opp_dist`) e virado para ele (`opp_bearing` até 90 graus); a fala é escolhida pela classe (a pele) e quase
todas esperam o jogador mirar no personagem (`Drone_tag::in_sights`, que `Check_Target` liga e
`NDrone2_PreDroneControl` desliga 30 ticks depois). `state_a` é o passo (0xff ao entrar no estado), `state_b` o quadro
da próxima fala:

| Classe (peles) | Falas (`Drone_SfxInfo`) | Regra |
|---|---|---|
| 9 (0x500001b/4f) | 0x95 ou 0x96 | uma vez, quando o jogador mira |
| 0xd, as moças (0x5000015/20/22/23/24) | 0x58 + 4 x (0-10) + voz | a cada 10 s de mira; a voz pela pele (20: 0, 22: 1, 23: 2, outras: 3); as 7-10 de cada voz não têm script de rosto e, pedidas com rosto, ficam mudas (no PS2 também) |
| 0xe (0x5000019/21) | 0x84-0x8c | `DroneFunc_DoBondTalk`: uma por mira, 8 s entre elas, olhando 200 ticks |
| 0x10 (0x5000026/66) | 0x97-0x9a | a cada 8 s de mira |
| 0x11 (0x5000012/13/14) | 0x8e ("Enjoy the party.") | a primeira até 1 s depois da mira, depois a cada 8 s (e o intervalo da entrada, 15 s) |
| 0x12 (0x5000017/18) | 0x52-0x57 | sem mira: 2 s depois de entrar no estado, as seis falas em ordem, com 16, 2.5, 8, 4, 4 e 5 s entre elas (0x54 e 0x56 sem olhar nem rosto); depois solta a cabeça e acabou |

Antes de falar o personagem vira a cabeça para o jogador (`NDrone2_HeadTrackObj`, se o comportamento tiver a
propriedade 0x4c). Uma fala recusada (o intervalo da entrada, outra fala tocando) sem script de rosto não marca a
espera: tenta de novo no tick seguinte.

**As dicas (`Hint_Create/Update`, `engine/trigger.cpp`).** O posicionamento 0x33 cria um objeto do tipo 0x43 (com as
dicas ligadas, `DrawInfo[3]`, 1 desde `bootup_bootup`, e na dificuldade do parâmetro 2: 0 todas, 1-4 até ela, 5/6/7
só a 1/2/3): um volume que, tocado pelo jogador (a lista de acertos do objeto) com o canal do parâmetro 3 ligado (0
sempre) e o do 4 desligado, toca o som do parâmetro 1, mostra o texto do parâmetro 0 como mensagem de informação (180
ticks) e some; com o canal 4 ligado some calado. Na dificuldade 2 há 48 em 15 fases (24 em 07000014), todas sem som
no disco (07000012: "Press Action" diante do computador da partida).

**A música do EE.** O jogo avisa a música por eventos (`Music_Event(n, valor)` em `EventList[64]`, zerados a cada
tick): 0 e 1 a fase carregou (`Sound_Ready`), 2 o alerta (a IA: 5 inimigos que viram o jogador há pouco,
`NDrone2_SeenAndAttacking` pelos limites de `MusicVars`; 1 e 3 os canais de alarme, `DroneFunc_CheckAlarmRaised`),
3 e 8 o fim da missão (`Trigger_Calc`, `Mission_Update`), 5, 11 e 12 as cenas (o script que acabou, o que começou e
o fim da câmera dele, pelo hash), 6 e 7 a morte e a vitória, 14-16 eventos da IA; os outros números (13, 17, 30-37,
40-47) são o da sala por onde o jogador anda (`cel_tag::misc[9]`, `Player_Update`). `UpdateMusicalEvents` (de `SFXUpdate`, uma vez por VBlank)
acha a linha da fase em `MapMusic` (0x13 palavras: a faixa, o trecho inicial, os da morte, da vitória e do fim da
missão, os dois contadores do ataque e até 5 trechos de combate e 5 furtivos), pede a faixa (`SFXStartMusic`), põe o
estado da IA da fase em `MusicVars` (`MapDroneData`), conta o ataque (com o evento 2 = 1 por `+0x1c` ticks seguidos a
música entra em combate; sem ele, por `+0x20` ticks, sai) e chama a função da fase. As funções `UpdateMFX_*` são
máquinas de estado pequenas (`MusicTemp1-9` e alguns estáticos): o combate pelo rodízio da lista de combate
(`PickRandomCombat`), a volta pelo rodízio furtivo (`PickRandomStealth`, só saindo de um trecho de combate), trechos
fixos nas cenas de cada fase (07000006: a cena 0x6000071 faz o salto de fim e espera o gatilho de música das cenas, o
som 0x172) e o salto de fim da missão (o trecho de `+0x14` vindo de combate, senão o de `+0x18`, com o fade dos sons
fora de 07000006). Cada pedido vai ao IOP como `SFXJumpToMusicMarker(trecho, imediato)` e o último pedido fica em
`MusicLastJumpRequest`; `MusicLastJump` é o trecho que o IOP diz tocar (`SFXActiveData`). `MapMusicData` começa na
linha 0 (07000001) e fica na última fase com música: no menu principal a função dessa fase continua rodando, como no
PS2 (o driver do port ignora um trecho além da tabela da faixa que toca, que o IRX leria sem conferir).
2. **A interface com o IOP (EE, `SFX*`, 36 funções, `platform/psi_sound.h`):** cada `SFX*` escreve o seu comando no
   lote (`SFXBatchCommands`, 0x800 palavras; cheio a partir de 0x79d, quando força um `SFXUpdate`); `SFXUpdate` (uma vez
   por VBlank) fecha o lote com 0x19 (o estado das cenas), 4 e 5 e o manda pelo RPC; os comandos lentos (carregar o
   banco, pausar as streams) vão num segundo lote, na hora. O RPC do PC é uma chamada direta a `nf::sfx_iop_rpc`, que
   devolve `SFXActiveData` como o IOP. Divergência: `SFXReset2` manda o lote se o 0xf ficou nele (no PS2 o reset
   espera o IOP; no PC ele é instantâneo e o lote seguinte sairia vazio).
3. **O driver (IOP, `SFX.IRX`, `platform/sfx_iop.h`):** 152 das 247 funções (56 de 85 KB), com o marcador
   `// [irx] 0x<endereço> <nome>` (o mesmo fluxo da decompilação, com `tools/ghidra_irx.sh`): o servidor dos comandos,
   os itens (`SFXActive`, 40), as vozes (`psiRequestVoiceHandle`, os limites e o roubo por prioridade), o volume
   (`ES_CombineVolumes`), o 3D e o 2D (`PS2_SFXCalculate3D`, a raiz de Newton do driver com o seu defeito), as amostras
   em sequência, em laço e polifônicas, o pitch aleatório, o ducker, os fades, a pausa (pitch 0) e as cenas
   (`PauseInNis`, o fade de `NisQuitten`), o reverb por sala, as falas (dois canais) e a música (o canal 0, pelos
   marcadores). O que não foi portado é o que o PC faz de outro jeito: o CD e o sistema de arquivos assíncrono, as
   threads (o relógio, a transferência), os buffers das streams (`ES_UpdateStreamMemCpy`, `UpdateMusicMarkers`,
   `SFXInitialiseStreamUpdate`: a fala é lida inteira na hora e a música aos poucos pela `MusicStream`, que segue a
   mesma linha do tempo de marcadores), os prints de depuração e o `__divdi3`.

Abaixo do driver, a **SPU2 em software** (`platform/spu2.h`): 48 vozes, 2 MB de RAM de som, PS-ADPCM com as flags de
laço e de fim, pitch de 14 bits (0x1000 = 48 kHz) com interpolação cúbica de 4 pontos (a SPU usa uma gaussiana de 4
pontos: divergência pequena, de timbre), o envelope que o driver programa (ataque de 3 amostras, soltura de 2), os
volumes por voz (com a fase invertida da voz de trás), os dois núcleos como o `sceSdInit` os deixa (cada um satura a
soma das suas vozes; a saída do núcleo 0 entra no núcleo 1 pelo `AVOL`; os volumes mestre e de efeito de cada um, o
de efeito com 16 bits: ver `formats.md`, "A mistura da SPU2"), um reverb "studio C" do `LIBSD.IRX` por núcleo
(o preset da área de trabalho e dos registradores tirado da `.data` do módulo; o reverb roda a 24 kHz, como no PS2),
o corte em 16 bits e um bloqueio de DC na saída (o capacitor da saída analógica do PS2: sem ele, a pausa, que congela
as vozes em pitch 0, deixava um nível contínuo). A saída (`platform/audio_out.h`) é **determinística**: antes de cada
RPC, `audio_out_catch_up(VBlankCount)` faz a SPU andar 800 amostras (48000 / 60) por VBlank passado (no máximo 600 de
uma vez, depois de uma carga longa), como se o IOP tocasse em tempo real. Os mesmos quadros saem para o XAudio2 2.9
(estéreo de 16 bits a 48 kHz, 50 ms adiantados, até 8 pedaços na fila), para um WAV (`--audio-dump`) ou para lugar
nenhum (`--no-audio`, o padrão das capturas, do `nf_loader_check` e do `nf_tests`); o relógio do driver
(`psiSFXGetTimer`, de 100 em 100 ms no PS2) vem dos VBlanks. Assim uma captura com `--audio-dump` tem sempre o mesmo
som.

O que se ouve (medido nos WAV, `python tools/sound.py analyse`): a cena de abertura de 07000001 com as falas de Kiko e
Mayhew (janelas de -24 a -30 dB RMS sobre o ambiente a ~-40 dB, nenhum corte; a fala 0x1e9 aos ~10.5 s, com a
legenda); o tiroteio de 07000012 (os tiros 0x430 dos inimigos e 0x412 do jogador, impactos, ricochetes, cápsulas; picos
de 0 dBFS e ~340 amostras cortadas em 14 s nos momentos de muito tiro, a saturação da própria SPU2: ver `port.md`,
"O nível da saída") com a música
`MFX_8` (o trecho 13, e o salto imediato para o 12 quando o jogador morre, que toca ~4 s e acaba); a música
`MFX_1` de 07000005; uma porta (0x11d em 07000006, ~-20 dB RMS); o menu principal (a música 0x1d7 e os sons dos
botões); os créditos com a faixa `MFX_16`; a pausa em silêncio e o som voltando ao continuar; o volume da música a 35
em vez de 70 baixa 12.3 dB (a fórmula ao quadrado prevê 12.2).

### Menus (portados, `src/engine/menu*.cpp`)

O sistema de menus é do próprio jogo e vale para tudo: o menu principal (pacote 07000048), a pausa, o
fim de missão, o aviso de controle desconectado. O arquivo de menu é o item tipo 8 do pacote (formato em
`formats.md`, "Menus (item tipo 8)"): `08000001` em todas as fases e `08000002` no menu principal.
`LoaderProcess` entrega o arquivo a `MenuManager_Load`, que guarda o endereço em `file_start`.

**Gerentes.** Cinco `M_MANAGER` (0x1d8 bytes, `manager` em 0x002e3cd8; o port os chama de
`menu_manager`): o jogo usa o 0 (um por jogador no multijogador). `MenuManager_Create(menu, página,
jogador, gerente, ação de fechar, pausa os sons, status)` percorre o arquivo e cria todas as páginas do
`menu` pedido (só as de plataforma 0 ou 3; as outras são de outros consoles), com os controles, os
scripts, os quadros-chave, as mensagens e os conjuntos de componentes delas; põe o cursor (um sprite
de `CursorInfo`), mostra a página pedida e dá o foco ao primeiro controle de id 0x10000002. O status
diz quem abriu: 2 o aviso de controle desconectado (página 0x4000002f), 3 a pausa, 4 o fim de missão, 5 o menu principal.
`MenuManager_Delete` fecha: devolve a ação de fechar ao jogador (`Menu_SetAction`), despausa o jogo se
ela é o Start (0x1e), avisa a página que ela saiu e libera tudo. A cada tick, `Game_Run` chama
`MenuManager_Update` (a entrada, os scripts dos controles, o tick da página, as mensagens atrasadas) e
`MenuManager_Monitor`, que fecha o menu cuja ação de fechar foi apertada e, fora do menu principal, a
cada 10 ticks confere os controles: se o de um jogador some, troca os menus pelo aviso e, quando ele
volta, abre a pausa (ou o fim de missão). A pausa em si é aberta por `Player_Update` quando o jogador
aperta Start.

**Controles e mensagens.** Tudo é um `M_CONTROL` (cabeçalho de 0x94 bytes: lista, sprites, dono,
componente, id, flags 0x10 normal/0x20 foco/0x40 apertado, tag, scripts, processos, retângulo,
profundidade, estado 1 escondido/2 desabilitado, tipo em +0x7b). Tipos: 1 botão, 2 caixa de marcar,
5 rótulo, 6 lista, 7 gerente, 8 página, 9 rádio, 10 barra de rolagem, 12 caixa de texto (o teclado da
tela, em `text.cpp`), 13 janela, 200 memo e 0x72 o sub-controle de um componente (o destaque da lista,
o cursor da barra); 3 combo e 11 spin não aparecem no disco. `__Menu_SendMessage(controle, mensagem,
a, b)` despacha pelo tipo; o que o tipo não trata vai para o dono. `__Menu_Send(gerente, id, ...)`
acha o controle pelo id na página atual. As mensagens de configuração (0x10/0x11 linha nova, 0x18
texto, 0x1a-0x1c cores, 0x1d/0x1e escolher linha, 0x23 formato, 0x24 imagem, 0x2b estado, 0x33-0x35
consultas) vêm do arquivo e dos tratadores; os eventos (0x4b escolheu, 0x4c entrou na página, 0x4d
saiu, 0x49 mudou, 0x4e/0x4f ganhou/perdeu o foco, 0x50 tick da página, 0x5d/0x5e os outros botões,
0x5f voltar) sobem até o gerente, que os passa a `Handler_HandleMessage`. Este acha o tratador do id
do controle (o original é uma árvore de comparações sobre 103 ids; o port usa a tabela `kHandlers` de
`menu_handlers.cpp`): `P_*` para páginas, `C_*` para controles; devolver 1 encerra o evento.

**Entrada.** `Menu_InputAction` lê as ações do controle (25 Start ou X, 26-29 direcional, 30 Start, 31
X, 32 quadrado, 33 círculo, 34 triângulo), com as manias do original: escolher e Start não valem
enquanto voltar está apertado e o contrário; nos menus de status acima de 3 o Start também escolhe.
`Menu_FindControl` move o foco pela direção: o controle mais próximo nessa direção (ordenado por
`QuickSort` com `COMP_MENUCTRL`), dando a volta no fim da página quando nada sobra; a navegação da
página (+0xc0: 1 todas, 2 só cima/baixo, 3 só esquerda/direita) limita as direções.

**Desenho.** Cada controle tem sprites da camada 2D na câmera 7 (`Menu_CreateSprite`). Os
componentes (`M_COMPONENTSET`, até 14 `M_COMPONENT` por conjunto) são as molduras do arquivo:
pedaços de uma textura 03xxxxxx, cada um com retângulo, coordenadas de textura, cor e flags (estados em
que aparece; bit 1 preso à direita; os que esticam acompanham o tamanho do controle);
`Component_SetupInstance` os põe no retângulo do controle. Os rótulos escrevem com as fontes do jogo
(`Label_Update`, sprite de texto com o formato da mensagem 0x23); as listas têm até 32 linhas de 5
colunas, com o destaque e a barra como sub-controles. O arquivo está em 640 x 480 e `FixupResolution`
converte para a tela do PS2 (512 x 448: x * 0,8 em torno de 256, y * 0,9333 em torno de 224).

**Scripts.** Um controle pode ter scripts (`M_SCRIPT`, 0x20 bytes) de quadros-chave (`M_KEYFRAME`,
0x28 bytes: quadro, retângulo, flags) com mensagens (`M_MESSAGE`); `Script_RunFrame` interpola o
retângulo (linear ou Catmull-Rom, com o `min.S` do R5900) e manda as mensagens do quadro (0xe1 em
diante criam um processo de cor, `Process_Create`, que esmaece as cores do controle). Os scripts
fazem as entradas e saídas animadas das páginas e o piscar dos destaques.

**Menus de jogo.** A pausa (página 0x4000004b, menu 0x80000002; `Player_Update` a abre com Start e
`GS_PauseGame(1)`): `P_PAUSE_Handler` e o painel `C_GCPAUSE_Handler` (0x10000028), com as abas
MISSION (continuar, recomeçar e sair, os dois últimos com a caixa de confirmação de
`Menu_CreateOptionBox`), OBJECTIVES (a lista dos objetivos da fase), CONTROLS (o estilo dos botões e a
inversão do olhar) e SCORE (as estatísticas; no multijogador, o placar). O fim de missão
(`P_ENDMISSION_Handler`, página 0x40000042, menu 0x80000004): a lista 0x100001a4 com o valor de cada
linha, 0 tenta de novo (reinício quente, `Player_RamLoad` com o bônus de vida), 1 recomeça a missão do
começo, 2 sai para o menu principal (página 0x4000001c); depois o fade e o menu fecha. O resultado
(`P_NFRESULTS_Handler`, 0x40000036 no menu principal): pontuação, medalha alcançada e a próxima, grava
a medalha (`Menu_SetLevelBonus`) e continua para o bônus (0x40000038, `P_NFBONUS_Handler`), o fim do
jogo (0x40000053) ou o mapa das missões (0x4000001c); com um codinome do cartão (`ls.slot` > 1) e uma
pontuação melhor (`PlrStats_DoneBetter`), antes pergunta "Do you want to save your progress?" (0x1000206,
YES na linha 0): YES põe `ls.slot = 999` e `ls.op = 1` e o tick da página (0x50) roda o fluxo do cartão
(`Menu_UpdateMessageBox`, a pergunta de sobrescrever) até `ls.done`; NO segue sem gravar.
`P_NFSTATS_Handler` (0x40000037) mostra as estatísticas. `GS_PauseGame` liga `GameState.paused` e a pausa de cada jogador (`MPGame`), e marca o
tempo nas estatísticas (`PlarStat_LogTimerPause`).

**As opções do jogo.** No menu principal, "Edit Codename" (0x4000001d, a roda `C_SBCNOPTIONS_Handler`)
leva às páginas de opções, todas com os valores guardados em variáveis globais:

| Página | Tratador | Opções | Onde fica |
|--------|----------|--------|-----------|
| "Advanced Options" 0x4000002d | `P_CNOPTIONS_Handler` | Vibration, Auto Aim, Crosshairs, Weapon Auto Switch, Flashing Objects, HUD Always On (On/Off); Crouch e Manual Aim (Toggle/Hold) | bytes de `PlayerSetting[0]`: +0x09 vibração, +0x01 mira automática, +0x08 mira na tela, +0x04 agachar alterna, +0x03 mirar alterna, +0x0a troca automática, +0x0c objetos piscando (2/0), +0x0b HUD sempre ligado (0 = sim) |
| "AV Options" 0x40000031 | `P_CNAVOPTIONS_Handler` | Music Volume e Effects Volume (barras de 0 a 20, x5), Subtitles, Multiplayer Split Screen (Horizontal/Vertical), Speaker (Mono/Stereo/Surround), Widescreen, Screen Adjust, Restore Defaults, Credits, o trailer de Die Another Day | `SFXMusicSetVolume`/`SFXSetVolume`, `DrawInfo[2]`, `DrawInfo[0]`, `SFXSetMode` (0/1/4), `DrawInfo[1]`, `psiSetScreenPos` |
| "Controller Setup" 0x40000022 | `P_CNCONTROLS_Handler` | 8 estilos de botões (NightFire, Moonraker, Octopussy, Goldfinger, Dr. No, Thunderball, GoldenEye, Classic Bond) e o eixo Y (Normal/Inverted) | `PlayerSetting::style` (+0x0e), `PlayerSetting::invert_look` (+0x00) |

Confirmar marca `cn_modified_flag` e volta; sair sem confirmar desfaz o volume e a posição da tela.
"Restore Defaults" põe música 14 e efeitos 20, legendas ligadas fora dos idiomas 0-3 e 6 (inglês
britânico e americano, francês, alemão, espanhol), tela dividida horizontal, estéreo e widescreen desligado.
`Menu_UpdateDefaultCodename` copia as opções para o perfil do codinome (`def_codename`, 0x38 bytes por
perfil: volumes, legendas, tela dividida, as opções de `PlayerSetting`, as medalhas), que o cartão de
memória grava; o widescreen não vai no perfil. Na pausa só há o estilo dos botões e a inversão (aba
CONTROLS).

**Páginas só do port.** Uma página nova usa as mesmas estruturas: `Page_Init` +
`Manager_SendMessage(gerente, 0x43, id, menu)` cria a página, `Manager_SendMessage(gerente, 0x42,
&NEW_CONTROL, 0)` cada controle (id, tipo, retângulo em 640 x 480, profundidade, formato), as mensagens
de configuração preenchem textos e linhas, e os componentes podem vir de um conjunto já carregado.
O tratador é registrado com `Menu_PortRegisterHandler(id, nome, função)`; os ids registrados valem antes
da tabela do jogo (o port pode até trocar um stub) e os novos devem vir depois dos do disco (páginas
depois de 0x40000053, controles depois de 0x10000244). É o caminho das opções do PC: a linha OPTIONS da
pausa e a página de opções em abas 0x40000060 (`engine/menu_pc_options.h`, com `MenuPc_RegisterPage`,
`MenuPc_ShowPage` e `MenuPc_AddControl` para outras páginas, e `MenuPc_RegisterTab`/`MenuPc_AddRow` para
as abas, como as de controles e teclas; ver `port.md`, "Opções do PC no menu"). O mouse dos menus (só do
port, `engine/menu_mouse.h`) entra no começo do tick do gerente e usa o cursor dele: ver `port.md`, "Mouse
nos menus". Logo depois entra o gancho `Menu_SetPortTick`, dos menus no estilo Modern (só do port,
`engine/menu_modern.h`): eles leem o estado dos menus do jogo e mandam as escolhas pelos mesmos caminhos (o cursor,
a 0x1d das listas, os botões do controle por um tick); ver `port.md`, "Menus no estilo Modern".

**Divergências** (comentadas no código): o rótulo do cursor é o endereço de "" no PS2 (0x0030d610),
recusado por `Txt_BindLabel`, como no original; `__Menu_Send` desiste depois de 1000 passos, como no
original; o formato de texto de um rótulo (0x23) vai para um buffer de 16 bytes com `snprintf`; uma
barra de faixa zero divide por 1 (guarda); `List_SetIndex` com lista vazia não faz nada; o botão sem
rótulo devolve nulo em 0x2f; o memo para em 32 linhas; `Handler_HandleMessage` com controle nulo
devolve 0. Guardas contadas em `g_menu_guards` e `g_menu_handler_guards` (zero nos dados reais).

### Menu principal (portado o caminho de um jogador, `src/engine/menu_frontend.cpp`, `menu_codename.cpp`, `loadsave.cpp`, `src/platform/psi_card.cpp`)

O boot (`bootup_bootup`) deixa `GameState.menu_page = 0x40000034` e `Reset_MapLoadSettings` pede o pacote
07000048; no fim da carga dele o `ResetMap_Load` cria o menu 0x80000002 nessa página (status 5). Sem `--level`, o
`nightfire.exe` deixa o pedido e o menu do jogo roda com os tratadores dele. O caminho:

| Página | Tratador | O que faz |
|--------|----------|-----------|
| 0x40000034 idioma | `P_LANGUAGE_Handler`, `C_LANGUAGE_Handler` | na versão americana (`bDisplayLangMenu` 0) carrega `LanguageToLoad` (`Txt_SetLanguage`) e segue; senão mostra as bandeiras de `FlagsToDisplay` |
| 0x40000048 | `P_PS2MEMCARDINIT_Handler` | `psiDoBootupCheck`: sem cartão ou sem espaço, a mensagem (OK / tentar de novo) |
| 0x40000032 | `P_INTRO_Handler` | os filmes 07370048 e 07380048, depois "Press START" |
| 0x40000009 | `P_START_Handler` | o texto legal; na primeira vez 240 ticks sem entrada até "Press the START button"; o rótulo de tela cheia tem o script de escolher, que mostra 0x4000004a; 45 s sem entrada mostram a demonstração (0x40000035, `P_ATTRACT_Handler`, e volta) |
| 0x4000004a | `P_PARISENUM_Handler` | lista o primeiro perfil do cartão (`Menu_EnumSaves(.., 1)`); as legendas dos perfis padrão (`Menu_WillRunMiniMission`: desligadas em inglês, francês, alemão, espanhol e na versão americana) |
| 0x40000043 | `P_ESTHERO_Handler` | o filme 073b0048 e o menu principal |
| 0x40000002 | `P_MAIN_Handler` | NightFire (`C_GONIGHTFIRE_Handler`: 0x40000025), Multiplayer (`C_GOMULTIPLAYER_Handler`: 0x40000019), Codenames (`C_GOCODENAMES_Handler`: 0x4000001b), cada um com o fade para preto |
| 0x40000025 | `P_NFSELECT_Handler`, `C_SBNFCN_Handler` | a roda dos codinomes da campanha: "New Codename", "Play without saving" (o aviso, caixa 9) e os do cartão (lido; no fim, a dificuldade) |
| 0x40000023 | `P_NFDFCTY_Handler`, `C_SBNFDFCTY_Handler` | a dificuldade (`difficulty`: Operative 1, Agent 2, 00 Agent 3) em `GameState.difficulty` |
| 0x4000001c | `P_NFMAP_Handler`, `C_SBNFMAP_Handler` | o mapa das missões (`sp_level`), na última liberada; escolher uma: `GameState.load_file`, o multijogador desligado, `ResetMap_LevelToLoad(fase, 0, 0)` (com o filme de abertura), o fade e o menu fecha; uma missão de carro mostra "LOADING..." três ticks e pede o DRIVING.ELF |
| 0x4000001b | `P_CNSELECT_Handler`, `C_SBCNSELECT_Handler` | os codinomes: escolher (os padrões ou um do cartão, lido), Y cria, X apaga (com a pergunta do cartão) |
| 0x40000020 | `P_CNNAME_Handler`, `C_KEYBOARD_Handler` | o teclado da tela: 0-9, A-Z, espaço, Del e End, até 8 letras; a primeira tecla apaga o "BOND" sugerido; End confere o nome (`Menu_IsValidCodename`: sem os espaços das pontas, em maiúsculas, sem conter "NEW CODENAME" ou "DEFAULT"), zera os recordes (`PlrStats_ResetScoring`) e grava o perfil novo; depois o menu do codinome |
| 0x4000001d | `P_CNMENU_Handler`, `C_SBCNOPTIONS_Handler` | "Edit <nome>": Secret Unlocks (o teclado em modo de código), as páginas de opções, "Save Codename" (grava o do cartão ou guarda as opções no perfil padrão e volta à página da tag da roda) |
| 0x4000002a | `PS2_DoCardFlow`, `C_LBERROPTIONS_Handler` | a caixa do cartão durante uma operação |
| 0x40000053 | `P_WINGAME_Handler` | depois da última missão: o filme 073f0048 e os créditos |
| 0x4000002b | `P_DOSSIER_Handler`, `C_SBDOSSIER_Handler` | o dossiê (o quadrado no mapa, no resultado e no bônus; ver "O dossiê") |
| 0x4000003a-3d | `P_DSRECORDS/DSREWARDS/DSGADGETS/DSWEAPONS_Handler` | os recordes, as recompensas, os aparelhos e as armas |

As rodas são barras de rolagem minúsculas (16 x 32 no arquivo) com o foco e cinco rótulos (tags 0-4, o do meio é o
item da vez; `Menu_UpdateWheel` para os itens `M_ITEM` de 0x18 bytes: imagem, nome, descrição, valor, liberado,
descrição de bloqueado; `Menu_UpdateCodenameWheel` para `codename_buf`). O "íris" é a imagem ao lado da roda
(030000bf-030000c3 indo e voltando, um passo a cada dois ticks): `Menu_StartIris` abre, fecha ou gira, e
`Menu_ChangePageCloseIris` fecha e troca de página 10 ticks depois (com as flags de `ls.next_flags`).

**O perfil e o cartão.** `ls` (`MENU_LS`, 0x002e1ad0) diz a operação do cartão em andamento (op 0 ler, 1 gravar, 2
listar, 3 apagar, 4 listar só o primeiro), o perfil em uso (`slot`: 0/1 os padrões de `def_codename`, 2+ um do
cartão, 999 nenhum ou novo), o nome, a página seguinte e se a lista de nomes está velha. `Menu_UpdateMessageBox` faz
um tick da operação (`LS_Load`, `LS_Save`, `Menu_EnumSaves`, `psiDeleteData`) e `PS2_DoCardFlow` mostra a caixa
0x4000002a enquanto o cartão tem uma mensagem ("Loading data...", "Saving data...", as perguntas; no PC com os textos
de `docs/port.md`, "Textos do PC no lugar dos do PS2"); no fim fecha a
caixa (o evento 0x6a vai à página de baixo: é assim que um codinome lido segue para a dificuldade ou o menu do
codinome) e mostra `ls.next_page`. A caixa fica pelo menos 60 ticks, fora das páginas do nome, do menu do codinome e
do resultado. Peculiaridade mantida: gravar sem `ls.can_save` (o menu principal liga) ou com o nome vazio desiste e
deixa a trava `recurse_160` ligada, e o cartão para de responder.

O arquivo do perfil (`LS_*`, `engine/loadsave.h`) é uma sequência de blocos (rótulo, tamanho em bytes, bits a
partir do bit 0x40, `BIN_PushBits`), um por entrada de `SaveIFFBlocks` (montada por um construtor estático do ELF):
PLRS as opções do jogador, MSSN as missões liberadas, o recorde e os momentos Bond de cada missão (as tabelas de
`PlrStats_GetScoreTable`) e o jogo terminado, MPSG as do multijogador, GSET volumes, idioma, legendas, modo do som,
widescreen, tela dividida e a posição da tela, CHET três trapaças, BNUS as medalhas e bônus (64 bits). `LS_Save`
monta o arquivo (os blocos fora da máscara vêm do último lido) e `psiSaveData` acrescenta o `crc32buf`.

O cartão (`MemCard_*`, `HandlePS2CardState`) é uma máquina de estados em pilha: cada passo chama a libmc e espera o
`sceMcSync`. Um perfil é a pasta `BASLUS-20579<NOME>` com o ícone (`007.ICN`, o item tipo 15 do pacote do menu), o
`icon.sys` e o arquivo de dados; depois de gravar, o jogo confere a pasta (5 entradas somando 78 KB) e, ao ler, o crc.
Gravar sobre um codinome que existe pergunta se sobrescreve; apagar pergunta antes.

**Filmes.** As páginas de filme (`P_INTRO`, `P_ATTRACT`, `P_ESTHERO`, `P_TRAILER`, `P_FMVPLAYER`, `P_WINGAME`) tocam o
filme delas pelo `psiStartBackgroundMovie` portado (`platform/psi_movie.h`; formato em `docs/formats.md`, "Vídeos";
o tocador do PC em `docs/port.md`, "Filmes") e esperam `psiMovieFinished`; `MenuManager_Update` toca o fundo em laço de
cada página (`Menu_PlayMovie(filme, 0, 1, ...)`, 07330048 no menu principal e 07350048 nas outras 40 páginas do
`08000002` que têm filme) e pula um filme pulável com Start (`Menu_StopMovie`, depois de `movie_counter` ticks). Com um
filme tocando, o `main` roda a lógica a 30 ticks por segundo e decodifica um quadro antes de cada tick
(`psiDecodeBackgroundMovieFrame`). Sem filmes (`--no-movies`, as capturas) ou sem o decodificador, o filme não abre e as
páginas seguem no tick seguinte, como antes.

**Só do port.** O multijogador mostra uma caixa "not available yet" (`C_GOMULTIPLAYER_Handler`). Uma missão de carro
(09xxxxxx, o DRIVING.ELF) chega a `GF_LAUNCH_DRIVING`; depois do `psiLaunchDriving` (um stub) o `GameFlow_Main` chama
`Menu_PortDrivingSkipped`: a missão conta como feita sem medalha (`Menu_SetLevelBonus(fase, 5, 0)`, a seguinte
liberada) e o menu principal volta ao mapa com um aviso. O multijogador fica com o tratador nulo. As páginas de
opções do codinome, os créditos e os códigos de "Secret Unlocks": ver "As opções do codinome".

### O dossiê (`src/engine/menu_dossier.cpp`)

O quadrado (mensagem 0x5d) abre a página 0x4000002b: do mapa das missões (`C_SBNFMAP_Handler`, com o íris), do
resultado e do bônus (`P_NFRESULTS_Handler`, `P_NFBONUS_Handler`). `P_DOSSIER_Handler` abre o íris e põe a roda no
começo quando vem de uma dessas três páginas; voltando de uma seção, o íris fica parado. A roda (`C_SBDOSSIER_Handler`,
`Menu_UpdateWheel` sobre `ds_options`, 4 `M_ITEM` do ELF) mostra os recordes (0x4000003a) e as recompensas
(0x4000003b) na hora, e os aparelhos (0x4000003c) e as armas (0x4000003d) depois do íris fechar
(`Menu_ChangePageCloseIris`).

| Página | Tratadores | O que mostra |
|--------|------------|--------------|
| 0x4000003a recordes | `P_DSRECORDS_Handler`, `C_RBDSRECORDS_Handler` | um rádio com as 12 missões de `sp_level` (todas), na última jogada (GameState +0x48); por missão o recorde (`PlrStats_GetLevelTotals`: `SCORETABLE::best`), os momentos Bond (a linha 0), as melhorias ganhas nela (recompensas de tipo 4 com o bit) e a melhor medalha com a imagem (0x300013d-0x3000140; cada uma só com todas as de baixo) |
| 0x4000003b recompensas | `P_DSREWARDS_Handler`, `C_RBDSREWARDS_Handler` | as quatro recompensas da missão (`Menu_GetLevelBonuses`: bronze o cartão, prata uma roupa/cenário/modo do multijogador, ouro uma melhoria, platina outra roupa), imagem e texto, escondidas até a medalha delas e as de baixo |
| 0x4000003c aparelhos | `P_DSGADGETS_Handler`, `C_SBDSGTSCROLL_Handler` | a roda de `ds_gadgets` (14): imagem, nomes e descrição, mais a linha da melhoria do grupo (grapple 3, câmera 6, dardo 0xc, decodificador 0xf, stunner 0x12, laser 0x15); a imagem do Shaver é a da versão do vídeo (0x300018f a 60 Hz) |
| 0x4000003d armas | `P_DSWEAPONS_Handler`, `C_SBDSWPSCROLL_Handler` | a roda de `ds_weapons` (27); o primeiro item, a pistola, troca de imagem, nome e texto pelo nível de melhoria dela (0 Wolfram PP7 ... 3 Gold P2K); o rifle de precisão (grupo 9) ganha a linha da melhoria 1 ou 2 |

As imagens são texturas 03xxxxxx do pacote do menu num rótulo de 150 x 150 ao lado da roda, como no mapa das missões:
nada de modelos 3D nem de câmera especial. `Menu_GetObjectUpgradeLevel(id, jogador)`: a contagem do grupo `id` (o bit
base das recompensas de tipo 4; a pistola aparece em três missões, o rifle em duas) na primeira medalha de tipo 4 do
grupo que o jogador tem, procurando missão a missão.

### Trapaças e a página de depuração (`src/engine/menu_cheats.cpp`)

Os tratadores C_CHCH* são das caixas de marcar da página 0x40000007 do menu de jogo (`08000001`, menu 0x80000002),
criada com toda pausa e nunca mostrada pela versão americana: nenhum código monta o id 0x40000007 e nenhum script do
arquivo a mostra; a lista de combinações de botões (`ComboKeysList`, que `Input_Check4Combo` percorre a cada
`Input_Update`) só é inicializada por `Input_Ready` e fica vazia. No disco cada caixa tem o texto "1". Cada tratador,
ao ser criada a caixa (0x51), mostra a global; escolhida (0x4b), grava o valor da caixa nela:

| Id | Global | Efeito |
|----|--------|--------|
| 0x1000021d | `bDebugControls` (1 no boot) | nada no executável lê |
| 0x10000019 | `debug_scanmode`, `debug_clipmode` | o jogador 0 no subestado 5 (`Player_SSScanMode`, o voo de depuração) ou de volta a 0; `debug_clipmode` nada lê |
| 0x100000e7 | `CheatInfo` +4 e +8 | marcada: armas 1-0x47 e aparelhos 0x4a-0x5e com 999 tiros (`Player_EquipWeapon`), colete 50; `Player_CheckWeaponsLoaded` |
| 0x100000e8 | `CheatInfo` +0 | invencível (`Player_HandlePain`) |
| 0x1000001c | `hud_state` (1) | cada jogador: `PlayerSetting` +8 (a mira), `MPSettings` +0x28 e `HUD_Enable` nos 22 painéis |
| 0x1000001a | `switch_SHOW_COORDS` | as coordenadas no painel da vida |
| 0x10000012 | `DrawInfo[1]`, `switch_CONFIG_WIDESCREEN` | 16:9 e `Camera_CalcViewAngles` das câmeras 0-8 (60 graus) |
| 0x10000015 | `switch_NO_DRONES` (o contrário da caixa) | `Drone_InitComms` pausa e esconde os personagens |
| 0x10000016 | `switch_BLIND_DRONES` | personagens cegos e surdos |
| 0x10000017 | `Drone_DebugSpr` | nada no executável lê |
| 0x100000bd | `Drone_DummyPlayer` | os personagens miram num jogador falso |
| 0x100000c7 | `switch_AllowFreeze` | `Game_Run` congela com `FreezeGame`, que nada liga |
| 0x1000001b | `SoundInfo[0]` | o volume da música, 100 ou 0 (`SFXMusicSetVolume`) |
| 0x10000101 | `switch_ForceDrawAll` | as salas sem portais (até as 64 de `VisCelList`) |
| 0x10000018 | `Set.214` | o jogador 0 no subestado 9 (`Player_ZeroG`) ou de volta a 0 |
| 0x10000169 | `bDebugLockup` (1) | nada no executável lê |
| 0x100001f7 | `bTweakBrightness` | nada no executável lê |
| 0x10000216 | `menu_unlock_everything` | o mapa das missões, as dificuldades e os extras do multijogador liberados |

A página tem ainda a lista das fases (0x10000010, `C_LBPMMAP_Handler`), o rótulo da versão (0x100000d6, que
`P_PAUSE_Handler` preenche com `VersionString`), "Previews" (0x4000000c, `P_NIS_Handler`), "Total Score:" (o rótulo do
disco num botão que abre os ajustes 0x40000044) e um botão sem nome que abre 0x4000004c (`P_CHEATMEDAL_Handler`:
escolher na lista fracassa a missão, canal 99, ou a cumpre, canal 100, guardando a medalha em `medal_from_cheat`, que
nada lê; a lista vem vazia do disco, então escolher dava 0, fracassar). A lista das fases, "Previews" e os ajustes:
ver "As opções do codinome". O bloco CHET do perfil grava `CheatInfo` +0,
+4 e +8 (invencível e as armas).

O que a versão de venda libera: as medalhas (os 64 bits de `bonus`: `Menu_SetLevelBonus` no resultado liga a medalha
ganha e as de baixo; as de tipo 4 viram melhorias por `Set_Upgrade`), as fichas "007 Bonus" (só com a medalha de ouro
da fase, `PlrStats_HasGoldMedal`; contam na pontuação, que decide a medalha) e os códigos de "Secret Unlocks" (o
teclado do codinome em modo de código: `Menu_SpecialCodenameCheck`, que monta a lista de códigos e chama
`Menu_UpgradeCheat` para ligar bits de melhoria).

O voo de depuração (`Player_SSScanMode`, subestado 5): sem queda, o analógico direito gira (pi rad/s), andar e de lado
vão no plano do jogador (suavizados, até 0,5 e 0,225 por tick), pular e agachar sobem e descem 3 m/s; mirando, o
direcional muda o campo de visão (0,2 a 3,14 rad). A colisão do corpo continua (`Player_CollisionHandler` só pula os
acertos). A gravidade zero (`Player_ZeroG`, subestados 8 e 9; a fase 0700001b começa em 8): sem queda, giro de
2pi/5 rad/s (mirando, dividido pelo zoom), velocidade nos três eixos do jogador suavizada (4% por tick, até 0,05) e
acumulada, então ele flutua; usar endireita o jogador por 1,5 s (`Mat_Align2Up` da frente no plano horizontal e
`Quat_Slerp_Acc`); no 9 o corpo anima como andando. Algumas constantes do ELF não são o float mais próximo do
decimal (0,225 é 0x3e666667, 0,04 é 0x3d23d70b): o port as usa pelo padrão de bits.

**Só do port.** Os nomes das caixas e do botão sem nome (`PcText` Dbg*), as seis linhas da lista de 0x4000004c,
`--debug-menu` (F9 numa fase abre a pausa e mostra a página por cima, `Menu_PortShowCheats`) e `--unlock-all`
(`Menu_PortUnlockAll`: `Menu_SetLevelBonus(missão, 4)` em cada missão de `sp_level` e o resultado nos dois perfis
padrão de `def_codename`, +4 as missões, +8 as medalhas, +0x19 o jogo terminado).

### As opções do codinome (`src/engine/menu_options.cpp`, `menu_secret.cpp`, `menu_tweaks.cpp`)

A roda do menu do codinome (`C_SBCNOPTIONS_Handler`) abre as páginas de opções do jogo, desenhadas no estilo Original
pelos tratadores dele:

| Página | Tratadores | O que faz |
|--------|------------|-----------|
| 0x40000022 "Controller Setup" | `P_CNCONTROLS_Handler`, `C_RBCONTROL_Handler` | a roda 0x10000025 com os oito estilos (os nomes do texto: NightFire ... Classic Bond) no estilo do jogador; o controle desenhado (`Menu_DisplayControllerStyle`: os rótulos 0x10000062-0x1000006e, 0x100000c8 e 0x10000163 com o que cada botão faz no estilo, rótulos consecutivos no *Txt.dat a partir de um por estilo; as linhas 0x10000061 escondidas e as do estilo, um bit por estilo na tag, mostradas); o círculo alterna "Normal"/"Inverted" (`was_inverted`); X grava o estilo (`Menu_ChangeControllerStyle`) e a inversão (`PlayerSetting::invert_look`) e volta. O jogo a mostra logo depois de criar um codinome |
| 0x4000002d "Advanced Options" | `P_CNOPTIONS_Handler` | uma roda por byte de `PlayerSetting[0]`: +9 vibração, +1 mira automática, +8 a mira sem mirar, +4 agachar e +3 mirar alternando (1, "Toggle") ou segurando (0, "Hold"), +0xa troca automática de arma, +0xc itens piscando ("On" 2, "Off" 0), +0xb o HUD sempre visível ("On" 0, "Off" 1) |
| 0x4000002e "Multiplayer Options" | `P_CNMPOPTIONS_Handler` | as do jogador no multijogador, gravadas no perfil: o radar (`MPSettings` +0x28), a vantagem de vida (+0x2c: -75 a +100) e a mira automática (`autoaim_mp`) |
| 0x40000031 "AV Options" | `P_CNAVOPTIONS_Handler` | as barras de volume da música e dos efeitos (0 a 20, o volume / 5; a cada tick o volume segue a barra, para ouvir), as legendas (`DrawInfo[2]`), a tela dividida (`DrawInfo[0]`: horizontal 0, vertical 1), o som (`SFXSetMode`: mono 0, estéreo 1, surround 4), o widescreen (`DrawInfo[1]`), "Screen Adjust", "Restore Defaults" (música 70, efeitos 100, legendas como o idioma pede, horizontal, estéreo, sem widescreen e a tela no centro, `psiRestoreScreenPos`), "Credits" e o trailer |
| 0x40000047 o ajuste de tela | `P_SCREENADJUST_Handler`, `C_SBSCREEN_Handler` | duas barras de -1 a 1 paradas no meio: cada passo é um `psiAdjustScreenPos` (x de 12 em 12 até ±0x1b0, y de 4 em 4 até ±0x30); X fica com a posição, voltar põe a de antes |
| 0x40000030 os créditos | `P_CREDITS_Handler` | a música 0x27 (`SFXStartMusic`); a cada 14 ticks a linha seguinte entra em y = 0x200 nos rótulos 0x10000213 (nome) e 0x10000214 (cargo) de tags 0-25, reusados em roda, em duas colunas ou numa linha larga; todos sobem 2 pixels por tick; nas últimas 50 linhas a música baixa; 18 linhas depois da última a página escurece e volta (depois do fim do jogo, `P_WINGAME_Handler`, vai ao menu principal) |

Nessas páginas, escolher (X) em qualquer controle confirma a página inteira (`cn_modified_flag`, e o menu do codinome
pergunta antes de sair sem gravar); voltar desfaz (na AV Options os volumes voltam aos de antes). Peculiaridade
mantida: a roda de widescreen não é limpa antes das opções, e cada visita vinda do menu do codinome acrescenta mais um
"On" e um "Off".

As linhas dos créditos (`Menu_SetupCredits`, 578 de 12 bytes: nome, cargo, a fonte de cada um, uma linha só) são
montadas por 22 KB de código no original. O port roda esse código do ELF na partida, no interpretador MIPS
(`game/menu_tables.cpp`: `Txt_BindLabel`, `Menu_Malloc`, `amemcpy` e `memset` nativos, o rótulo devolvido marcado), e
guarda o resultado em `CreditsSource`: 209 rótulos do texto e 497 strings do `.rodata`; o `Menu_SetupCredits` portado
monta as linhas dessa tabela, como o original, a cada página.

**Os volumes.** `SFXMusicSetVolume/GetVolume`, `SFXSetVolume/GetVolume` e `SFXSetMode/GetMode` são a interface do driver
de som no IOP: cada Set manda um comando e guarda o valor (`MusicMasterVolume` 70, `SfxMasterVolume` 100, `SFXModeNumber`
1 no disco). No PC (`platform/psi_sound.h`) o comando vai ao driver portado (`platform/sfx_iop.h`), que toca com eles
(veja "Som"): o volume da música e dos efeitos se ouve na hora, também durante a página (a barra da AV Options muda o
volume a cada tick), e o modo mono tira a fase invertida da voz de trás. A página AV Options, o bloco GSET do perfil e o
`nightfire.ini` usam esses valores. `SoundInfo[0]` é outra cópia, a da trapaça da música.

**O que no PC só é guardado** (decidido com o usuário): o widescreen (`DrawInfo[1]`: o jogo troca a projeção para
16:9 anamórfico; o PC desenha na proporção da janela, Hor+, e troca a proporção depois de `Camera_CalcViewAngles`), a
tela dividida (`DrawInfo[0]`, multijogador) e a posição da tela (`psiSetScreenPos`: no PS2 move a imagem na TV; no
PC a janela não tem margem de TV). Os valores chegam ao lugar do jogo e ao perfil, e nada no desenho os lê. O modo do
som (`SFXSetMode`) agora vale: o driver do IOP portado o usa (mono, estéreo, surround).

**Os códigos de "Secret Unlocks"** (o teclado do codinome em modo de código, `C_KEYBOARD_Handler`):
`Menu_SpecialCodenameCheck` monta na pilha, a cada chamada, a lista dos 52 códigos com o estado atual: os de missão
põem todas as missões (`PASSPORT`) ou trocam uma (um "ou exclusivo" na máscara de `sp_level`), os de bônus trocam ou
ligam bits de `bonus` (os personagens, cenários e modos do multijogador), e os de armas pedem melhorias a
`Menu_UpgradeCheat` (liga as medalhas cujas recompensas de tipo 4 são a melhoria pedida, até a quantidade pedida).
Digitar de novo um código de "ou exclusivo" desfaz. O `nf_loader_check` roda o `Menu_SpecialCodenameCheck` do ELF no
interpretador para cada código (e dois que não valem) com três estados de `bonus` e das missões, com o
`Menu_UpgradeCheat` portado, e compara com o portado: `codinomes especiais: 162 de 162 conferem com o ELF`.

**A página de depuração** (0x40000007, com `--debug-menu`; ver "Trapaças e a página de depuração") também leva a:
"Total Score:" (0x40000044, `P_TWEAKS_Handler`: os multiplicadores de dano `DroneDamage_*` x2 e x4, `DroneArmour_*` x5,
`Plr_DMod_*` x10 e x20 em barras, o valor ao lado com "%f", X grava, o quadrado vai a 0x40000046,
`P_TWEAKS2_Handler`: o tiro da IA, `DroneFiring`), "Previews" (0x4000000c, `P_NIS_Handler` e a lista `C_NIS_Handler`:
70 itens, títulos e scripts de cena; escolher um script carregado na fase o toca por cima do jogo pausado, com o
jogador parado e as câmeras guardadas) e a lista das fases (`C_LBPMMAP_Handler`: no disco só "Main Menu"). Os itens
das duas listas também vêm do ELF na partida (o código 0x51 deles no interpretador).

**As opções do jogo e o `nightfire.ini`** (`platform/game_options.h`). Cada opção mora no lugar dela no jogo
(`PlayerSetting[0]`, `DrawInfo`, os valores do som) e as telas do port a leem e escrevem por
`nf::game_option/set_game_option`. O `.ini` lembra entre execuções as que têm sentido no PC: `[controls] vibration`,
`pad_style`, `pad_invert_y`, `[ui] subtitles` e `[game] auto_aim`, `crosshairs`, `crouch` e `manual_aim`
(`toggle`/`hold`), `weapon_auto_switch`, `flashing_objects`, `hud_always_on`, `music_volume`, `effects_volume`.
Precedência: (1) na partida (`psiInput_Init`) as chaves que o `.ini` tem valem por cima dos padrões do PS2; (2) um
codinome do cartão lido põe as opções dele, como no jogo, e o `.ini` não muda (`game_options_rebase` em
`LS_LoadPlrSettings` e `LS_LoadGlobalSettings`); (3) os perfis padrão ("New Codename", "Play without saving",
"Default": `Menu_MapDefaultCodename`, e as legendas que `Menu_WillRunMiniMission` desliga na versão americana) pegam
as chaves do `.ini` por cima de `def_codename`: no PC o `.ini` faz o papel do perfil padrão, que o PS2 perde ao
desligar; (4) uma opção mudada num menu (o jogo ou o port; qualquer diferença desde a última referência, vista a cada
tick no começo de `psiInput_MapInputs`) vira a do `.ini`, gravado na hora; só a opção mudada. Tudo isso só com um
`.ini` para gravar (`nf::g_controls_persist`: fora das capturas, ou com `--config`); sem ele o port é o PS2.

## Campanha: o que falta para completar

Auditoria das 21 fases a pé das missões (`MissionData`; as de direção, `09xxxxxx`, o port pula). O
`nf_loader_check` monta, para cada fase de missão, uma linha `campanha:` (`tests/campaign_audit.cpp`) com o que a
fase pede e o port ainda não tem; `nf_loader_check <pasta> [fase] --campanha` acrescenta a lista completa dos
provisórios (`provisorios_por_funcao=`) e dos stubs chamados na carga e nos 2 s de jogo (`stubs_carga=`,
`stubs_jogo=`). No fim, `bloqueios da campanha (fases):` ordena os bloqueios pelo número de fases que cada um trava.
`python tools/scripts.py canais <pacotes>` lista os canais que os scripts de cena ligam (eventos 13 e 17).

O que a linha olha:

- **provisórios**: cada posicionamento cujo `*_Create` ainda passa por `pending_create` (port: `g_pending_placements`
  guarda o tipo e os parâmetros, também os sem modelo);
- **subestados** que a fase pede ao jogador: escada (posicionamento 0x22, `Ladder_Create`), corda e tirolesa (0x3e,
  `Wire_Create`: o parâmetro 1 é o tipo do fio, 0 corda, 2 tirolesa), gancho (0x3a, `Grapple_Create`; só bloqueia se a
  fase dá o aparelho), esgueirar (0x3f, `CreepWall_Create`), os ícones de toque (0xfa, `ThirdIcon_Create`: o tipo é o
  `BLData::touch_type` que liberam, 2 gancho, 3 corda, 6 esgueirar) e a água (salas com `cel_tag::water_y`; nadar já
  anda, subestado 3). Nenhum script de cena das fases a pé cria escada (entidade do tipo 0x22: `tools/scripts.py
  stats`), então o `Ladder_Create` dos scripts não pesa na campanha;
- **aparelhos**: as classes de aparelho do inventário inicial (`Player_InitWeapon`) e, com `->alvo!`, os que a fase
  pede mas cujos alvos ainda são provisórios: o gancho (0x50) atira só com `touch_type` 2 e prende num ponto 0x51; a
  câmera (0x54) fotografa um `CamSubject` (0xfd), que liga o canal do parâmetro 0; o decodificador (0x56) abre os
  cadeados (`Lock`, 0x2e) dos tipos 0 e 2 (`Player_WeaponFiring` exige `BLData::target` num objeto 0x3a); o laser
  (0x4e) queima os cadeados do tipo 1 (`Lock_Update`); o Q-Worm (0x58) age num `PCQWorm` (0x35, objeto 0x49);
- **objetivos**: cada canal de objetivo da fase (`OBJECTIVE_tag::channel`, "(nao)" = não deixe acontecer) e quem o
  liga: os objetos portados vivos (gatilho, troca de fase, interruptor, fusíveis, script player, destrutível, item,
  gerador, porta, a morte de um personagem ou de um modelo de gerador: `param12`) e os provisórios pelo parâmetro do
  canal de saída tirado da decompilação de cada `*_Update` (`Lock` 5, `Sensor` 3 e 8, `SS` 3, `PCQWorm` 1, `CamSubject`
  0, `Copter` 7 e 8, `Searchlight` 3, `SpaceLaser` 5, `Wire` 3). "?" é um canal que nenhum objeto da fase liga: um
  evento de script de uma seção carregável (os pacotes NIS não entram no `scripts.py canais` do pacote da fase) ou a
  lógica de outra fase da missão; entre colchetes, os posicionamentos que têm o valor em algum parâmetro;
- **canais provisórios**: os canais de saída de objetos provisórios que alguém espera (porta trancada ou acionada,
  entrada de gatilho, script player, interruptor, personagem, gerador, destrutível, objetivo) e que nenhum objeto
  portado liga. Os alarmes não contavam (o sensor que vê o jogador, os holofotes: sem eles a fase só ficava mais
  fácil); agora são portados (ver "Câmeras de segurança, monitores e holofotes");
- **stubs_jogador**: as funções não portadas do jogador e dos objetos de jogo chamadas na carga e nos 2 s.

### Antes do porte (o ponto de partida)

| Fase | Subestados | Aparelhos com alvo provisório | Objetivos e canais só de provisórios | Outros objetos provisórios |
|------|------------|-------------------------------|---------------------------------------|----------------------------|
| 07000001 | | | | sensor, monitor, 16 SS, atirador, corpos rígidos |
| 07000002 | 4 escadas, 1 corda, água | | | sensores, monitor, 15 SS |
| 07000003 | 1 escada | | | 8 SS |
| 07000004 | | | | |
| 07000005 | escada, corda, 2 ganchos, 2 esgueirar | gancho | | 29 holofotes, 2 helicópteros |
| 07000006 | | | | 28 SS, 20 holofotes, inversor |
| 07000007 | | | | |
| 07000008 | 2 escadas, tirolesa | laser -> cadeado | objetivo 75 e gatilho: helicóptero; canais 134, 135: cadeados | 2 SS |
| 07000009 | | | | 2 SS |
| 0700000a | escada, gancho | gancho, decodificador -> cadeado, Q-Worm | objetivo 72 (cena + Q-Worm); portas 48, 49: cadeados | 6 torretas, sensores, monitor |
| 0700000b | | decodificador -> cadeado, Q-Worm | objetivo 76 (cena + Q-Worm); porta 47: cadeado; gatilho 100: helicóptero | 5 torretas, sensores |
| 0700000c | 2 escadas, 3 cordas, tirolesa, 5 ganchos | laser -> cadeado, gancho, câmera -> foto | objetivo 28: foto; gatilhos 14-17: cadeados de laser | 8 SS |
| 0700000d | 3 escadas, corda, 2 tirolesas, 3 ganchos, esgueirar | gancho, câmera -> foto | objetivo 30: foto | gunimp |
| 07000011 | gancho | gancho | porta 18: cadeado | Q-Worm, 2 SS |
| 07000012 | gancho | gancho | porta 10: cadeado | 3 torretas, 5 sensores, 8 SS, helicóptero |
| 07000013 | | | | |
| 07000014 | 3 ganchos | gancho, decodificador -> cadeado | gatilhos 44, 45 e porta 60: cadeados | 14 sensores, 12 monitores |
| 07000015 | 3 escadas | decodificador -> cadeado | portas 149, 247, 249: cadeados | 9 minas |
| 07000016 | escada, água | | | |
| 0700001b | | | | lasers e mísseis da estação |
| 0700004a | | | | 2 atiradores |

Ranking (fases travadas por módulo): escada 9 (02, 03, 05, 08, 0a, 0c, 0d, 15, 16); cadeado 8 (o decodificador e o
laser, e as portas e gatilhos que esperam o canal: 08, 0a, 0b, 0c, 11, 12, 14, 15); gancho 7 (05, 0a, 0c, 0d, 11, 12,
14); corda e tirolesa 5 (02, 05, 08, 0c, 0d); torreta 3 (0a, 0b, 12); foto 2 (0c, 0d, objetivos); Q-Worm 2 (0a, 0b,
objetivos); esgueirar 2 (05, 0d); helicóptero 2 (08, 0b; o objetivo 75 de 08). Esta é a ordem do porte.

Os stubs que todas as fases chamam a cada tick (`Player_GrappleSetRope`, `Player_LaserPointer`, `Player_SetWatch`)
não bloqueiam: o primeiro é o desenho da corda do gancho; os outros, o laser de mira e o relógio do Q-Watch (portados
depois: "Armas do jogador").

### Depois do porte

Tudo o que a tabela de cima marcava foi portado com o código do jogo, na ordem do ranking (escadas, cadeados, gancho,
cordas e tirolesas, torretas, fotos, Q-Worm, esgueirar, helicópteros) e mais os objetos de roteiro simples (SS), que
eram os provisórios mais numerosos. O `nf_loader_check` termina com `bloqueios da campanha (fases): nenhum`; os
provisórios de todas as fases caem de 763 (o `main` antes deste trabalho) para 441, sem mudar `inimigos=` em nenhuma
fase e sem guardas novas. A linha `campanha:` ganhou as provas (`travessia:`), cada uma depois dos 2 s de jogo, com o
jogador invencível e o estado global restaurado no fim; as coordenadas `x,y,z,giro` que elas imprimem servem de `--at`
para capturas.

A última coluna já está sem o que o trabalho seguinte portou (as câmeras de segurança, os monitores, os holofotes, as
minas, os atiradores, os inversores, o canhão montado, o tremor e a estação espacial: ver "Câmeras de segurança,
monitores e holofotes" e as seções depois dela).

| Fase | O que as provas fizeram (nf_loader_check) | Objetivos com "?" | Provisórios que sobram |
|------|-------------------------------------------|-------------------|------------------------|
| 07000001 | 16 SS esperando toque | 71 | 4 corpos rígidos, cosméticos |
| 07000002 | 2 de 4 escadas subidas (3,90 m), corda pega; 2 SS disparam e andam | 77 | 21 corpos rígidos, cosméticos |
| 07000003 | escada subida (4,40 m); SS dispara e anda | 78, 83 | 3 corpos rígidos, chuva |
| 07000004 | | | corpos rígidos, cosméticos |
| 07000005 | escada (7,60 m), corda, 2 de 2 ganchos (de 29,6 m a 0,9 m do ponto); o helicóptero só ronda e vai embora | 70 | chuva |
| 07000006 | 27 de 28 SS disparam e andam | 2 | corpo rígido, cosméticos |
| 07000007 | | 73 | chuva |
| 07000008 | escada (2,66 m); 2 cadeados de laser abrem (canal 135); o helicóptero ataca e foge: canal 75 (objetivo) | | chuva |
| 07000009 | 2 SS | 71 | chuva, rotor |
| 0700000a | decodificador abre 1 de 3 cadeados (canal 48), Q-Worm: canal 72 (objetivo); 6 torretas automáticas, 1 abre, 368 tiros | 75 | chuva, rotor |
| 0700000b | decodificador (canal 47), Q-Worm: canal 76 (objetivo); helicóptero explode (canal 102); 5 torretas, 2 destruídas | 89, 90 | chuva, marolas |
| 0700000c | 2 escadas, tirolesa (desceu 11,5 m, 24,9 m de fio), corda, 2 de 8 ganchos, 4 de 5 cadeados de laser, foto: canal 28 (objetivo) | 35 | rotor |
| 0700000d | 2 de 3 escadas (8,62 m), tirolesa (36 m de fio), corda, 5 de 5 ganchos, parede de esgueirar, foto: canal 30 (objetivo); SS | | 3 corpos rígidos, rotor |
| 07000011 | 1 de 2 ganchos (de 11 m a 3,3 m) | 100, 72 | 3 rotores |
| 07000012 | helicóptero ataca e explode (canal 101); 8 SS; metralhadoras montadas abrem e atiram | 77 | rotor |
| 07000013 | SS | | rotor |
| 07000014 | decodificador abre 1 a 3 de 3 cadeados (canal 60) | 71, 72, 74 | |
| 07000015 | decodificador abre 1 ou 2 de 3 cadeados (canal 149); as 3 escadas estão fora de toda sala | 142 | |
| 07000016 | escada (4,58 m) | | |
| 0700001b | | 11 | |
| 0700004a | | 74 | |

As falhas das provas não são do porte: as 3 escadas de 07000015 e a de 0700000a estão fora de toda sala (o centro e a
partida não caem na caixa de sala nenhuma: um poço sem geometria abaixo do túnel, uma escada abaixo do piso), então
nenhum teste de colisão as alcança, no original também (`escadas_fora_de_sala=`); os ganchos que não puxam (0700000a,
07000014: uma grade do script player, entidade `0x02000750`, logo acima do ícone; 07000012: uma entidade grande de
script, `0x0200058f`) batem num objeto que a história tira do caminho antes; a tirolesa de 07000008 e a parede de
07000005 falham só na sequência da prova (numa captura com a fase recém-carregada, a tirolesa de 07000008 pega e desce:
abaixo); os cadeados que o decodificador não abre são os de teclado ou os que esperam a chave do parâmetro 1, e quantos
ele abre varia de uma rodada para outra com o sorteio dos códigos (a prova dá um tempo fixo a cada cadeado).

Provas por captura (`nightfire.exe --data <pasta> --fixed-video-hz 60 --width 1024 --height 576 --screenshot ...`; o
relatório `.txt` tem `jogador x y z giro r subestado s` e `canais`):

| Prova | Comando (`--level`, `--at`, `--input`, `--wait`) | Resultado |
|-------|---------------------------------------------------|-----------|
| Escada | `07000005 --at 59.43,27.10,89.21,1.571 --input "none:0.3 W:7"`, `--wait 0.5 / 3 / 8` | y 27,10 → 30,08 aos 3 s (subestado 1, subindo) → 34,63 aos 8 s (subestado 0, saiu por cima): 7,53 m |
| Tirolesa | `0700000c --at 46.83,21.53,-205.50,0 --input "none:0.5 space:0.05"`, `--wait 1.5 / 3 / 8` | subestado 15 (y 18,09 e 12,23), solta no fim aos 8 s em y 9,76: 11,8 m abaixo e 22,6 m adiante, com a câmera de terceira pessoa |
| Tirolesa | `07000008 --at 133.43,48.14,-101.21,-1.571 --input "none:0.5 space:0.05"`, `--wait 1 / 3` | subestado 15, de x 133,4 a 110,9 em 2,5 s (os gatilhos do caminho ligam os canais 0x5a-0x5c e 0x68) |
| Foto | `0700000c --at 5.31,7.38,-112.12,0.002 --input "none:0.5 4:0.1 none:1.5 rmb:1.5 rmb+lmb:0.3 rmb:1"`, `--wait 5.5` | "4" (o quarto aparelho) põe a micro-câmera (arma 84) na mão, o botão direito mira, o esquerdo fotografa o traje: canal 0x1c (28, o objetivo da foto) ligado |
| Q-Worm | `0700000b --at -19.71,47.88,30.70,2.710 --input "none:0.5 5:0.1 none:1.8 pad_ry=-1:0.25 none:1 lmb:0.3 none:3"`, `--wait 8` | "5" (o quinto aparelho) põe o Q-Worm (arma 88) na mão, olhando 23 graus para baixo, um tiro no computador: canal 0x4c (76, o objetivo) ligado |
| Q-Worm | o mesmo em `0700000a --at -17.33,1.10,-33.06,0.297` | canal 0x48 (72, o objetivo) ligado |

Os canais de objetivo ligam, mas a contagem de objetivos (`missao objetivos D/T`) só anda quando o objetivo já está à
vista: os objetivos da foto e do Q-Worm ficam escondidos (estado 1) até o gatilho deles, que a história liga antes
(entrar no prédio ST-1 em 0700000c, por exemplo); `Mission_MonitorObjectives` então os mostra já cumpridos.

O que uma partida inteira ainda pede e não dá para provar por roteiro: andar de sala em sala pela história (as provas e
as capturas mostram cada mecanismo no lugar dele, não o caminho entre eles); os objetivos com "?", ligados por eventos
dos scripts das seções carregáveis (NIS) ou pela lógica de outra fase da missão.

Ainda provisórios, nenhum ligando um canal que um objetivo ou outro objeto espere: os corpos rígidos (`RB_Create`) e os
cosméticos (chuva, marolas, raios, folhas, árvores, nuvens, rotores). As câmeras de segurança, os monitores, os
holofotes, as minas, os atiradores, os inversores, o canhão montado, o tremor e a estação espacial foram portados depois
(abaixo): os provisórios de todas as fases caem de 441 para 270, sem mudar `inimigos=` em nenhuma fase e sem guardas
novas. Depois vieram os corpos rígidos (ver "Corpos rígidos"): de 270 para 235, e sobram só os cosméticos. Por fim o
tempo e os enfeites (ver "Chuva, neve, raios e nuvens" e "Enfeites"): de 235 para 29, e nenhum nas fases da campanha
(a coluna "Provisórios que sobram" da tabela de cima está toda resolvida); os 29 são `Car_Create`, os carros das
arenas do multijogador (07000023-29, 4b) e um da 07000046.

O polimento da jogabilidade (a mira do tronco, a mira automática, os lasers e o relógio, a água, os corpos rígidos e
a metralhadora montável) tem a sua parte na linha `campanha:`, depois da segurança: `polimento: mira_auto=desligada:a
ligada:b visada:v dist:d em:x,y,z,giro` (o inimigo à vista mais perto, o jogador a 6 m na frente dele mirando 4 graus
ao lado do peito, cinco tiros com a opção desligada e depois ligada: quantos acertaram, pelos projéteis na lista de
acertos dele; 07000006, 0d e 12: 0 e 5; 07000013: 1 e 5), `corpos=N tipos=a,b,c,d,e com_colisao=N
pendulo=x,y,z,giro(tipo tiros inclinou andou)`
(um pêndulo leva tiros de pistola por 2 s a 4 m; 07000003: 2 tiros, 29 graus), `relogio=ponteiros:3 raio:02000131
quadros comprimento` (1 s atirando com o laser do relógio), `mira_laser=ligada feixe ponto` (a pistola 6 com a mira
laser), `metralhadora=montou:1 em:...` (com a pele da 0x52 na fase, um tiro dela monta uma torreta no chão: 0c, 12, 13)
e `agua=salas nadou submerso ar desceu ondulou` (o jogador 1,5 m abaixo da água da primeira sala com água, olhando para
baixo e andando 4 s; 07000016: nadou e ficou submerso os 240 ticks, o ar caiu a 78 e a câmera ondulou). Numa fase cuja
cena de abertura ainda toca, as provas religam o jogador antes (`Player_Enable`). `guardas_mira=N` aparece se
`AnimObjectAimAt` pegou um alvo nulo.

### Travessia (portada, `src/engine/player_traverse.cpp`)

Escadas, gancho, cordas, tirolesas, paredes de esgueirar, os ícones de toque e as câmeras de terceira pessoa, do
módulo Player (0x00192728-0x00198318). O cabeçalho de `player_traverse.h` descreve cada um; o resumo:

- **Escada** (posicionamento 0x22, ou entidade de script do tipo 0x22; objeto 0x2d): o modelo é a colisão.
  `Player_DealWithObjHit` chama `Player_CollLadder` quando o jogador encosta de frente (o giro da escada aponta para
  fora) entre a base (centro - raio + 1) e o topo (centro + raio + 0,85): ele fica meio metro à frente do centro, sem
  arma, no subestado 1 (`Player_Climb`). O analógico para a frente sobe 0,02 m por tick por unidade; acima do topo sai
  para a frente, abaixo da base ou no chão descendo sai; pular sem andar solta para trás; ao sair,
  `BLData::traverse_wait` (um segundo) impede de prender de novo e a arma volta.
- **Gancho** (0x3a, objeto 0x51): só com um ícone de toque do tipo 2 a arma 0x50 atira; o projétil que para num ponto
  0x51 (`Bullet_Delete` com `hit_flags` 0x40) chama `Player_SetGrapplePoint`, e o subestado 2 (`Player_SSGrapple`)
  puxa o jogador (0,05 m por tick a mais por tick, até 1,5) até 2 m do ponto, onde ele fica pendurado enquanto segura o
  gatilho; `Player_GrappleSetRope` estica a corda (entidade 0x2000138) da arma ao ponto.
- **Fio** (0x3e, objeto 0x2c): o parâmetro 1 diz o tipo: 0 corda (subestado 6, `Player_Wire`: pendurado, anda de lado,
  cansa em 30 s), 2 tirolesa (subestado 0xf, `Player_Zipline`: desce a 0,15 m por tick e solta perto da ponta de
  baixo; só pega na metade de cima), 1 desligado. Pega ao encostar ou pulando perto de um ícone do tipo 3; a câmera
  passa ao modo 0xb (`camera_tracking`) e segue a câmera de terceira pessoa (posicionamento 0xe7,
  `ThirdPersonCameras`); o canal do parâmetro 3 fica ligado enquanto o jogador está no fio.
- **Parede de esgueirar** (0x3f, objeto 0x2b): usar com um ícone do tipo 6 encosta o jogador de costas (subestado 7,
  `Player_Creep`), que anda de lado até a ponta.
- **Ícone de toque** (0xfa, objeto 0x42): com o canal do parâmetro 1 ligado, encostar põe o tipo (parâmetro 0) em
  `BLData::touch_type`, que `Player_CollisionHandler` zera a cada tick e `Player_WeaponFiring` lê; o HUD mostra o ícone.

### Cadeados, Q-Worm e alvos da câmera (portados, `src/engine/ssys.cpp`, `player_traverse.cpp`)

- **Cadeado** (0x2e, objeto 0x3a, `LOCK_tag`): o tipo 0 abre pelo teclado numérico (usar abre a página 0x40000021 do
  menu de jogo 0x80000003), o 1 pelo laser (as armas 0x4e e 0x4f esquentam o cadeado até ele abrir), o 2 só pelo
  decodificador. Todos abrem com a chave do parâmetro 1 (`Player_EquipKey`) e, os tipos 0 e 2, com o decodificador
  (0x56/0x57, o PDA), cujos tiros acham os quatro algarismos do código (`KeyCodes`, sorteados por `Locks_Init` no estado
  `GF_LOCKS_INIT`) um a um em `GlobalVars`. Ao abrir, o canal do parâmetro 5 liga, o modelo troca pelo seguinte e, com
  caminho, o cadeado anda; o tipo 0 tranca de novo se o canal desligar.
- **Q-Worm** (0x35, objeto 0x49, `PCQWORM_tag`): atingido pela arma 0x58 (com o parâmetro 3) ou usado (parâmetro 2),
  troca de modelo, revela o código do parâmetro 0 (uma mensagem; o teclado passa a mostrá-lo) e liga o canal do
  parâmetro 1.
- **Alvo da câmera** (0xfd, objeto 0x48): um projétil da micro-câmera (classe 0x54) o atinge, liga o canal do
  parâmetro 0 (guardado em `obj_tag::unknown_f6`) e some.

### Corpos rígidos (portados, `src/engine/rigid_body.cpp`)

O posicionamento 0xd8 (`RB_Create`, objeto 0x1a, `RIGIDBODY` de 0x160 bytes; 34 nas fases a pé: 4 em 07000001, 21 em
02, 3 em 03, 2 em 04, 1 em 06, 3 em 0d, mais 1 em 07000041) é um modelo com física: massa (parâmetro 0; 3 sem ela),
tipo (2), luz da fase que vai junto (3) e som de batida (4). Todos os do disco são do tipo 1: pêndulos presos pelo
centro (lanternas de papel na casa de Mayhew, peças penduradas em varais), que só giram. A inércia é a de uma caixa
com as medidas do modelo (`RB_CuboidMomentsOfInertia`); a orientação, um quatérnio escalar-primeiro que começa na
identidade (o giro do posicionamento se perde: `RB_SetState`).

A cada tick (`RB_Update`): debaixo da água da sala, bolhas e um empuxo (|y - topo da sala| x massa x 0,4: o topo, não
a água, é do original); o vento da sala (`Env_GetWindVel`, no ponto meia altura - raio acima do centro, o que dá
giro); a integração (`RB_EulerEvolve`): aplica os impulsos do tick (`RB_ApplyImpulses`: o angular passa pela inércia,
com os eixos y e z trocados, e é preso a +-4), o arrasto angular (omega += omega x -(1 - e^(-dt/25)) / dt, sem outro
dt: a forma do original), no tipo 0 a gravidade com arrasto linear de 1 s (velocidade final 9,8 m/s), gira o quatérnio
por omega (`RB_apply_omega`, o seno da VU0) e, nos pêndulos, um impulso que leva o "cima" de volta à vertical
((-cima.z, 0, cima.x) x I1 x `FRAME_RATE_MUL`). Na colisão (`RB_CollisionHandler`): o tipo 0 testa a esfera no
`Collide_Update` e `RB_RestingContact` dá atrito (`RB_CalcTorque`), tira metade da velocidade contra a normal (com o
coeficiente -0,5 a fórmula do original não inverte: o corpo assenta, não quica) e o carrega com outros objetos que se
movem; os pêndulos fazem a própria cápsula (`Collide_CylinderIntersect`, 1,8 raio abaixo até o centro, fora do tipo
3) e somam o torque do empurrão. Um tiro (o acerto de um projétil, objeto 5) toca o som e empurra no ponto do acerto
com a direção x o dano x 0,5; nos pêndulos só nos eixos do tipo (1 sem y, 2 só x, 3 só y, 4 só z). Com o parâmetro 3,
a luz da fase mais perto (`Light_FindMapLight`) vai com o pêndulo. Peculiaridade mantida: um tiro encerra a lista de
acertos do tick no tipo 0.

### Torretas (portadas, `src/engine/turret.cpp`)

Uma torreta (0x27, objeto 0x36, `GUNTURRET` de 0x100 bytes) são dois scripts de cena tocando juntos: o corpo (o
parâmetro 2, que mira) e a base (parâmetro 3); os streams 0, 1 e 2 do corpo se registram (`GT_Register`) como o
clarão, o cano e uma terceira peça. O tipo 1 é a automática (liga com o canal do parâmetro 4 ligado e o do 5
desligado, mira só no jogador, munição infinita); o 0 é a metralhadora montada (mira no jogador e nos personagens, 200
tiros; usar a desmonta e ela vira o item da arma 0x52). Estados: 0 recolhida, 1 abrindo (os scripts andam até a pausa
do fim), 4 ativa (`GT_Track` acha o alvo a até 50 m à frente; o corpo gira com um slerp e atira a arma 0x60 a cada
0,1 s), 2 e 3 recolhendo, 5 atordoada (taser: 3 s de giros ao acaso), 6 destruída (a vida, 100, acabou: fumaça por
10 s), 7 controlada pelo jogador (multijogador). `GT_DeployMiniGun` monta de novo a metralhadora desmontada: um tiro
da arma 0x52 (`Player_WeaponInitBullet`, um metro à frente do jogador; ou um bot, `DroneWeap_FireWeapon`) passa a
matriz do jogador por `Collide_PlaceObject` (raio 0,5, meia largura 0,6) e, cabendo no chão, cria uma torreta do tipo 0
com os scripts 0x6000045 (corpo) e 0x6000044 (base) e o modelo 0x2000359, virada para a frente dele; sem lugar, o jogador
recebe a bala de volta. O nível que o original monta na pilha só escreve os parâmetros 0 a 4 (o 5, o canal que a
desliga, é lixo no original; 0 aqui).

### Helicópteros (portados, `src/engine/copter.cpp`)

Cada posicionamento 0x2f é um caminho (`COPTERPATH`, lista `CopterPaths`) de um grupo (parâmetro 1): o tipo 0 também
cria o helicóptero (objeto 0x3b sem modelo, `COPTER` de 0x7c bytes na lista `CopterList`; as peças são as entidades do
script player do parâmetro 4, registradas por `Copter_Register`), o 2 é o caminho de saída, os outros são de ronda.
Escondido e parado até o canal do parâmetro 6. Ronda pelo caminho; a cada 4 s / `DiffMod` (1, 1,5, 2 ou 4 pela
dificuldade) procura o jogador num cone de 45 graus à frente (`Intersect_ConeSphere`; `COPT_COS` e `COPT_SIN` vêm do
construtor estático, calculados no port porque os símbolos não têm tipo no ELF), a até 1 km e à vista de um ponto 4 m
à frente; achando, ataca (`Music_Event` 2; metralhadora, arma 0x61 em 07000008 e 0x71 nas outras, ou mísseis, 0x62 e
0x6d) até 5 s x `DiffMod`, 100 de dano ou perder o jogador de vista. O dano é o dos tiros e explosões nas nove peças,
filtrado e multiplicado pela tabela `Bits` do ELF; acima da metade a peça 3 solta fumaça; acima de 299 x `DiffMod` ele
foge pelo caminho de saída (explode em 07000012 e 0700000b) e liga o canal do parâmetro 7. O canal do parâmetro 8 liga
depois de parâmetro 9 ticks; com 100 o helicóptero só ronda e some no fim do caminho (0700000b, 07000005).

### Objetos de roteiro simples (portados, `src/engine/simple_script.cpp`)

O posicionamento 0xe2 (objeto 0x25, `SIMPLESCRIPT_tag` de 0x2c bytes) é um modelo que anda pelo seu caminho
(`KeyFrame_Interp`, ou `Spline_Interp` com o parâmetro 6): portões, pontes, plataformas, hélices, a água que sobe. O
modo (parâmetro 0): 8 anda sempre em laço, 1 anda já, 0x40 espera um disparo, 0 parado. Disparos (`SS_Operate`): o
canal do parâmetro 3 ligado ou um toque das classes do parâmetro 2 (2 tiro, 8 o jogador, 0x10 um personagem); com o
bit 4, usar (`SS_Activate`). Sem o bit 1, o disparo liga o próprio canal. Para no quadro do parâmetro 5 (preso a um
número inteiro de passos) esperando outro disparo; no fim do caminho volta ao começo esperando, ou para de vez com o
parâmetro 1. Com o parâmetro 7, a altura da água da sala (`cel_tag::water_y`) segue a esfera do objeto. Toca o som do
parâmetro 4 ao disparar. Um caminho sem duração recebe 0x4b, escrito no próprio posicionamento como no original.

### Câmeras de segurança, monitores e holofotes (portados, `src/engine/sensor.cpp`, `searchlight.cpp`)

Do módulo SSys (0x001ad878-0x001b0968) e dos holofotes (0x001cb4f8, junto das luzes). Os cabeçalhos `sensor.h` e
`searchlight.h` descrevem cada estado; o resumo:

- **Câmera** (posicionamento 0x28, objeto 0x37, `SENSOR` de 0x64 bytes): varre o caminho do posicionamento
  (`Spline_Interp`, o servo 0x4dd no meio e no fim) com um cone (parâmetro 1, graus) e um alcance (2). Vê o jogador pelo
  feixe (o raio da frente acerta ele: `Sensor_SetBeam`, que também estica o modelo do feixe, parâmetro 0, até o que o raio
  bate, e gira o suporte, parâmetro 7) ou pelo cone com linha de visão (`Sensor_InCone`, `Intersect_ConeSphere`). Com a
  câmera de script (4) ativa, não vê. Vendo-o mais de 30 ticks, bipa (0x4a9) e segue (estado 1, `Vec_Track` 5% por
  tick, o bipe acelerando); depois de 2 s seguindo, liga o canal de alarme (parâmetro 3) a cada tick; sem vê-lo por mais
  de `FRAME_RATE_INT` ticks, volta ao caminho (estado 2, slerp de 0,1) e o alarme desliga (no estado 0 ele é desligado a
  cada tick: o alarme é "estar sendo visto"; o que ele liga nos gatilhos fica). O canal do parâmetro 4 desliga a câmera
  (estado 3: o modelo vira o LOD, o feixe some, o zumbido 0x4aa para). `Sensor_ApplyDamage` é vazia no jogo: uma câmera
  "destruída" é um destrutível que liga o canal do parâmetro 4 (07000014: o 0x53 das duas câmeras do canto).
- **Monitor** (posicionamento 0x30, objeto 0x38): usar (`Monitor_Activate`, de `Player_Activate`) mostra a primeira
  câmera do grupo (parâmetro 0 = `SENSOR::group`, parâmetro 6 da câmera) que não está desligada; usar de novo passa à
  próxima; depois da última o jogador volta (o monitor espera 6 ticks). A imagem: o PS2 põe a câmera do jogador na
  matriz da câmera de segurança a cada tick (`Mat_Copy` para `viewer_tag::camera`, sem função de atualização), para o
  jogador (`Player_Disable`, que também esconde a arma) e troca os painéis 0 e 1 do HUD pelo 16 (`HUD_UpdateSecCamPane`:
  os cantos e o "REC"); não há textura renderizada nem imagem no modelo do monitor, então o renderizador do port não
  muda (a câmera 0 é a desenhada). O modelo da câmera mostrada some da imagem dela (`unknown_f8` = 0, e os flags 0x10
  enquanto `SENSOR::viewed`); o canal do parâmetro 8 da câmera liga enquanto ela é mostrada. Com o parâmetro 1 = 0 o
  monitor quebra com um tiro (`Collide_GetDamageNObjects`, a explosão 0x6000027) e devolve o jogador.
- **Contagens do SSys** (`SSysItems`, itens 1-4: laser 1, laser 2, câmeras 1 e 2): um interruptor de script com o item
  grava o canal (`Script_CreateEntity`); com o canal ligado, `SSys_Monitor` (fora das cenas) conta 240 s, com as falas
  das tabelas `Laser1.134`, `Laser2.135`, `Cam1.132` e `Cam2.133` do ELF aos 180, 120, 60 e 6 s (`MIN3`..`MIN01`) e no
  fim, quando desliga o canal (o sistema de segurança volta). Em 0700000b, o laser 2 primeiro encerra o laser 1 e espera
  8 s; com o canal 0x76 desligado, só ele fala; em 0700000a as câmeras 1 não falam.
- **Holofote** (posicionamento 0xde, objeto 0x22, `SEARCHLIGHT_tag` de 0x40 bytes): até cinco peças no mesmo lugar pelo
  tipo (0 suporte, 1 lâmpada, 2 mancha, 3 feixe, 4 brilho), juntadas na lâmpada pela sala (a lâmpada vem primeiro no
  disco). A mancha ganha uma luz do tipo 2 (a "lanterna" de `Lights_CalcClosestLights`: branca, raio 500, ilumina os
  objetos com a flag 0x80 a até 4 m da mancha) e nasce 10 km longe. A lâmpada: apagada (estado 3) até o canal do
  parâmetro 1; varrendo (0) um círculo em volta do giro do posicionamento e do centro do arco (parâmetros 4 e 5); a
  mancha vai onde o raio de 100 m da lâmpada bate e, com o jogador a até 2 m dela, a lâmpada o segue (1: vira 15% por
  tick para os pés dele, presa ao arco, e liga o alarme do parâmetro 3 a cada tick; 15 s com ele longe da mancha voltam
  ao 0). O brilho aparece de frente para a cabeça do jogador (cosseno acima de 0,5), na lista 2 com linha de visão. Um
  tiro quebra a lâmpada (2: as peças e a luz somem, ela cai soltando fumaça); o canal do parâmetro 2 a apaga de vez.

Os alarmes nas fases (o `nf_loader_check` mostra a cadeia: `ligou=` com quem espera cada canal):

- 0700000a e 0700000b: o alarme de cada câmera (0x1d/0x1e; 0x27/0x28) entra num gatilho OU que liga o 0x12 (0x1c), o
  canal de ligar das torretas automáticas do teto: elas descem e atiram (`torretas:2`).
- 07000014: os alarmes das 12 câmeras armadas (0x3d-0x46, 0x4c, 0x4d) entram num OU que liga o 0xdf, e este num OU que
  liga o 0x96: o som do alarme (um gatilho de som), as luzes (emissores) e os guardas com `Drone_tag::channel_b` = 0x96
  (o parâmetro 6 deles), que passam ao estado de ataque (`personagens:11` na prova). Os monitores (12, grupos 1-5)
  mostram as câmeras; o 0x78 (um gatilho) desliga as câmeras armadas.
- 07000012: as 5 câmeras não têm alarme: são do monitor (grupo 0xa, indestrutível).
- 07000006: os quatro holofotes acendem com os canais 0x3d-0x40 da história e o alarme deles é o 0x37, que os guardas
  esperam (`personagens:27`); o inversor (0xb1 -> 0x37) apaga o alarme quando o 0xb1 liga.
- 07000005: só uma lâmpada tem mancha; ela acende com o 0xa4, o alarme da fase (a saída de um gatilho, que
  `DroneFunc_CheckAlarmRaised` também lê para a música): os holofotes procuram o jogador depois que os guardas dão o
  alarme.

As provas (`seguranca:` na linha `campanha:`, depois das da travessia; o jogador invencível, o estado restaurado no fim):

| Fase | O que a prova fez |
|------|-------------------|
| 07000001 | a câmera (sem alarme) vê o jogador; o monitor mostra 1 câmera e devolve o jogador; o atirador atira |
| 07000002 | as 2 câmeras (sem alarme, sem feixe) não o veem do ponto da prova; o monitor mostra as 2 |
| 07000005 | a lâmpada acesa à força acha o jogador na mancha, segue 2 s, a luz da mancha ilumina ali |
| 07000006 | 4 lâmpadas acham, seguem e iluminam; 3 ligam o alarme 0x37 e 27 personagens reagem |
| 0700000a | 2 câmeras veem, ligam o alarme; o 0x12 liga e 2 torretas saem do repouso; o monitor mostra as 2 |
| 0700000b | 2 câmeras veem, ligam o alarme; o 0x1c liga e 2 torretas saem do repouso |
| 0700000d | o canhão montado: monta, 200 tiros, superaquece, desmonta |
| 07000012 | 5 câmeras (sem alarme) veem; o monitor mostra as 5 |
| 07000014 | 14 câmeras veem, 12 ligam o alarme; 19 canais ligam (o 0x96 do som e dos guardas), 11 personagens reagem; 12 monitores mostram 40 câmeras |
| 07000015 | 2 das 9 minas vivas na hora da prova (as outras já explodiram nos testes de antes); uma explode com o toque; o atirador atira; 3 tremores |
| 0700001b | 8 tremores; o laser carrega e atira 2 vezes em 40 s; 2 mísseis se armam e 1 é lançado |

### Minas, atiradores, inversores e o tremor (portados, `src/engine/hazard.cpp`, `trigger.cpp`)

- **Mina** (posicionamento 0xfe, objeto 0x4b, `MINE_tag` de 0x30 bytes): gira e balança (parâmetros 2, 3 e 4; as de
  07000015 não) e explode com qualquer acerto de um objeto na lista dela (o jogador encostando, um personagem, um
  projétil, outra explosão): a explosão do script do parâmetro 5 com o dano do parâmetro 0 e o raio do 1 (o original
  tem `Explode_Create` inline: o port chama `Explode_Create` com os mesmos valores, a mina como causadora). As de
  07000015 são minas de laser presas às paredes (dano 80, raio 10 ou 15): andando, o jogador passa rente a uma sem
  acertá-la; um tiro a explode (a captura de docs/port.md: a besta, a explosão leva o jogador de 100 a 59,8 com o
  ajuste da fase, x 0,6 no Agent; 32,9 antes do `ReadTuningVars`), e o
  jogador posto no centro dela também (a prova).
- **Atirador** (0xd2, objeto 0x4e): com o canal do parâmetro 0 ligado, um tiro da arma do parâmetro 1 pela frente (para
  baixo se a classe da arma for 0x3b) e some. 07000001, 07000015, 0700004a.
- **Inversor** (0x41, objeto 0x53 sem modelo nem sala): com o canal do parâmetro 0 ligado, desliga a cada tick o do 1.
- **Tremor** (`Apocalypse`, 0x4a, objeto 0x54 sem sala): com o canal do parâmetro 0 ligado (uma vez basta), por
  parâmetro 1 segundos: a vibração (parâmetro 2), o sacudir da câmera 0 (3: `viewer_tag::shake`; o `Camera_Update`
  soma à posição dela `Rand_FRand_MVar2_Vec`, entre -tremor/60 m e 0 em cada eixo, a cada tick: ~1,5 cm por quadro no
  tremor de 07000016, com três sorteios do `Rand` por tick) e o clarão (4:
  `Player_SetFlashBang`, que o port agora tem: o borrão e o esmaecimento branco que `Player_Update` conduz),
  proporcionais ao que falta. 07000014-16, 0700001b.

### O canhão montado (portado, `src/engine/gunimp.cpp`)

`GunImp` (posicionamento 0x34, objeto 0x47, `GUNIMP_tag` de 0xa0 bytes): a base (o modelo do posicionamento, matriz do
quatérnio dele) e o cano (o modelo de `TurretGuns`, a tabela de 12 armas do ELF: boca, modelo, arma, atraso, som). No
jogo sozinho a arma é a 11 (o canhão de laser 0x67 de 0700000d). Usar monta (subestado 0xc, congelado, câmera no modo
0xe, sem arma, 30 graus de campo de visão); os analógicos miram (0,015 rad por tick, a inclinação entre -90 e +45 graus,
15% por tick), o jogador fica atrás da base; o tiro sai a cada `TurretGuns::delay` ticks, com o feixe até a parede
(`PositionBeam`, portado junto: os feixes das armas de laser dos projéteis também passam a ser posicionados), o clarão
do impacto e 5 de dano num raio de 4 m (`Explode_Propagate`); +2 de calor por tiro, 200 superaquece até esfriar. Usar de
novo, ou a morte do jogador (`GunImp_PlayerHasDied`), desmonta. O `nf_loader_check` monta o de 0700000d, atira 5 s (200
tiros, superaquece) e desmonta.

### A estação espacial (portada, `src/engine/space_station.cpp`)

- **Laser** (`Create_SpaceLaser`, 0x38, objeto 0x50 sem modelo): com o canal de ligar (parâmetro 7; 0x13 em 0700001b,
  a saída de um gatilho), a contagem do parâmetro 3 (165 s) corre; a cada 10 s um dos dois scripts de carga (o evento 15
  registra a boca: `SpaceLaser_Register`) mira onde o jogador estava, o som 0x5de da metade em diante, e o tiro: o raio de
  1 km, o projétil da arma 0x6b, o feixe por 5 ticks e as faíscas 0xc000010. No fim da contagem o script do fim toca em
  laço e, 65 s depois, liga o canal 99 (a missão fracassa); o canal 0xe (parâmetro 6) apaga o laser.
- **Mísseis** (`Create_SpaceMissile`, 0x39, objeto 0x4f): os oito do silo; com o canal 0x13, cada um se arma depois do
  seu atraso (parâmetro 3: 0 a 105 s), o painel 20 do HUD pisca (`MissileDeploy` = 1) e o script do silo toca até a
  pausa dele; então, com o canal do número do míssil (1-8) ligado — sabotado pelo jogador — toca o script do parâmetro 2
  e liga o canal do 6 (0x15-0x1c); senão é lançado (o script do 1) e liga o canal do 5 (0xc7). Os casos de
  `SpaceMissile_Update` vêm da tabela de saltos 0x002fcc30 (código fora do corpo que o Ghidra vê).

## Camadas do código

`tools/layers.py` classifica as 3.038 funções:

| Camada | Funções | Bytes | Fatia | Destino no port |
|--------|---------|-------|-------|-----------------|
| Lógica | 2.089 | 872.364 | 68,5% | decompilar e compilar como está |
| Lógica com VU0 inline | 233 | 192.064 | 15,1% | decompilar e trocar as instruções vetoriais por C/SSE |
| Plataforma | 228 | 90.804 | 7,1% | reescrever para PC |
| Runtime (libc, libm, GCC) | 137 | 50.000 | 3,9% | já existe no PC |
| SDK da Sony e kernel | 351 | 68.560 | 5,4% | substituído pela plataforma nova |

Critérios: o SDK é o código linkado a partir de `zerobss` (`0x002242a8`); plataforma é o que
chama o SDK, lê ou escreve registradores (`0x1000_0000`–`0x1200_FFFF`) ou o scratchpad, tem
prefixo `psi`/`PS2`, ou só é chamado por plataforma; VU0 inline é o que usa instruções COP2 ou MMI.
É uma estimativa: ponteiros para hardware calculados em tempo de execução não são detectados.

A engine já separa a plataforma: o prefixo `psi` marca a interface que o código comum chama.
A lógica usa só 106 funções de plataforma. As mais chamadas:

| Função | Chamadores na lógica | Papel |
|--------|----------------------|-------|
| `amemcpy` | 70 | memcpy alinhado a 128 bits |
| `PS2Sinf`, `PS2Sinf2`, `PS2Sinf3` | 52 | seno/cosseno em lote (VU0) |
| `psiMovieFinished`, `psiStart/StopBackgroundMovie` | 26 | vídeo MPEG (portadas: `platform/psi_movie.h`) |
| `psiSetTweakARGB`, `psiSetUpColourBlend` | 11 | estado de desenho |
| `psiDrawObjectMatrix`, `psiBuildMatrixPalette`, `psiDrawParticleList` | 14 | desenho de objetos |
| `psiLight_*` | 12 | luzes |
| `psiGetScreenPos`, `psiSetScreenPos` | 10 | posição da imagem na TV |
| `psiGetTimeIn100ths`, `psiInitTimeIn100ths` | 7 | relógio |
| `SFX*`, `PS2_SFX*` (9 funções) | 19 | áudio no IOP |
| `psiFileLoad`, `psiCopyToSP` | 5 | arquivos e scratchpad |
| `psiInput_*`, `psiFlushAllInput`, `psiMultitapPresent` (8 funções) | 11 | controles e vibração |

A lista completa sai em `out/layers_report.txt` (`python tools/layers.py`).

### VU0 em modo macro

233 funções de lógica usam a VU0 como coprocessador vetorial (`lqc2`, `vmul`, `vcallms`...).
As que mais usam são colisão e física: `Intersect_CylGeom` (202 instruções COP2),
`Player_Collision` (106), `Intersect_RayGeom` (92), `AnimFrameCopy` (73), `RB_CalcTorque` (49).
O decompilador as mostra como intrínsecos (`_lqc2`, `_vmulbc`, `_vcallms`). O port precisa de
um cabeçalho que implemente esses intrínsecos com SSE, e de versões em C dos microprogramas da
VU0 chamados por `vcallms` (skinning em `SkinIt`, por exemplo).

## Plano de port que sai deste mapa

1. **Plataforma nova atrás da interface `psi`**: implementar no PC as ~106 funções que a lógica
   chama, sobre o que já existe em `src/` (arquivos, renderizador D3D11, janela e entrada).
2. **Laço principal no PC**: reproduzir `main` sem o hardware e manter `GameFlow_Main` como está,
   com tempo de quadro vindo do relógio do Windows em vez do VBlank.
3. **Decompilação da lógica por módulo**, dos mais básicos aos mais altos: matemática (`Vec`, `Mat`,
   `Quat`), memória e hashtable, carga de fases (`Loader`, `parsemap`), objetos (`control_*`), jogador,
   IA (`NDrone2`, o maior módulo), HUD e missão (portados), menus e multiplayer.
4. **Intrínsecos da VU0** em C/SSE e microprogramas da VU0 reescritos.
5. **Áudio** (feito): o driver `SFX.IRX` roda no IOP; no PC ele foi portado inteiro (`platform/sfx_iop.cpp`) sobre
   uma SPU2 em software e o XAudio2, e a interface `SFX*` do EE manda os mesmos lotes de comandos (veja "Som").
6. **DRIVING.ELF** depois, como segundo módulo trocando o bloco PTP com o primeiro.
