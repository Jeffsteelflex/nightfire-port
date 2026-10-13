# DRIVING.ELF: as missões de carro (estudo)

Estudo do segundo executável do jogo, o das missões de veículo, antes de qualquer porte. Hoje o port pula essas
missões (`GF_LAUNCH_DRIVING` → `psiLaunchDriving`, um stub → `Menu_PortDrivingSkipped`; ver CLAUDE.md, "Front end").
Tudo aqui foi conferido nos arquivos do disco (NTSC-U) com as ferramentas citadas; o que é palpite está marcado.

## Resumo

- O `DRIVING.ELF` é **outro motor**, da EA, não da Eurocom: C++ (GCC 2.95.3 da SN Systems) sobre as bibliotecas da EA
  EAGL 4.06.01 (gráficos e animação), REAL (arquivos, memória, entrada, sistema), CARP (formato de cena), SND (som, com
  o `SNDDRV.IRX` no IOP) e o código do jogo (`BondPS2`, 208 objetos: `PBondCar`, `AIGroundVehicle`, `RPlayerCamera`,
  `GHud`, `SMissionManager`...). Os nomes sugerem a linhagem dos jogos de corrida da EA (`NFSPrintf`, `MATHNFS`,
  `RTyreTrack`) e dois estúdios: `GSystem::IsSavage` é verdade nas pistas da neve (`snow1a`, `snow2a`) e
  `GSystem::IsEARS` nas da Ilha (`junglea/b/c`), que usam galerias próprias (`sav_gall1.gal`, `ears_gall1.gal`);
  provavelmente Savage Entertainment e EA Redwood Shores (palpite pelos nomes).
- Com o `ACTION.ELF` ele divide **só o SDK da Sony** (as mesmas versões das bibliotecas) e o runtime do GCC/newlib. Nada
  do motor: nem o microcódigo da VU1 (o do `DRIVING` vem em arquivos de dados, 115 "render methods" do EAGL), nem o
  driver de som (`SNDDRV.IRX` da EA, não o `SFX.IRX`), nem os formatos (BIGF/RefPack/CARP/SHPS, não o `FILES.BIN`).
- O **`DRIVING.SYM`** é um mapa de símbolos simples (valor + nome), com 9.519 funções nomeadas, 447 classes com RTTI e
  os caminhos dos objetos do link. Mas é de **outra compilação** (14/10/2002, 15% maior; o ELF do disco é
  de 18/10): os endereços só batem no começo do `.text`. `tools/driving_sym.py` casa as duas: 3.455 âncoras estruturais
  e mais 1.655 funções alinhadas com o tamanho igual (97 a 100% certas nos testes) dão nome a 5.107 funções no projeto
  do Ghidra (`out/ghidra_driving`), 68% das funções do jogo; outras 2.551 ficam como sugestão em comentário.
- A **troca** entre os dois ELFs passa pelo `BASE.ELF`: o `ACTION` copia um bloco de 0xA50 bytes (o "UberBond":
  perfil, opções, dificuldade, a missão) para o endereço fixo 0x100000 e reinicia o `BASE`, que reinicia o IOP com o
  driver de som do destino e carrega o outro ELF por um carregador próprio (não `LoadExecPS2`). O `DRIVING` devolve no
  mesmo bloco o resultado (0 cumprida, 1 fracassada, 2 abandonada, 4 morte) e as 9 linhas da pontuação que o `ACTION`
  lê por `NewScoresRef`. A Ilha (13a → 13b → 13c) encadeia três execuções do `DRIVING` sem voltar ao `ACTION`.
- O **tamanho**: o código do jogo do `DRIVING` (1,25 MB, ~5.150 funções no ELF) já é do tamanho do `ACTION.ELF`
  inteiro, e há mais 0,8 MB de bibliotecas da EA, das quais uma boa parte também precisa de porte (EAGL, animação, som).
  O plano abaixo vai do primeiro carro na pista ao fim da missão em cinco fases; a estimativa honesta é de **duas a três
  vezes o esforço do porte do `ACTION.ELF` feito até aqui**, com incerteza alta. O maior risco técnico é o render (115
  microprogramas da VU1 em modelos que são pacotes de DMA para eles) e a física (microcódigo da VU0 e 4x mais código VU0
  em macro do que no `ACTION`).
- A **versão de Xbox** (seção 9), do mesmo código e da mesma semana, foi compilada pelo MSVC para x86 sem símbolos;
  `tools/xbox_match.py` casa 2.332 funções dela com as do PS2 (1.823 com confiança alta, ~97% certas nos testes): 57% dos
  bytes do código do jogo e 88% da física. A física dela é C++ comum, com o mesmo leiaute e as mesmas constantes, inclusive
  o que no PS2 é microcódigo da VU0: vira o oráculo do algoritmo (não dos bits) e baixa a estimativa para 27-41 dias.
- O **protótipo do desenho** (seção 10): o `nf_driving` desenha um carro do EAGL pelo caminho do jogo (o CARP, o
  `DynamicLoader`, o `GeoPrim::Draw`, o VIF, a família `MatCar` lida no microprograma da VU1 e refeita num shader). Os 63
  carros dos arquivos carregam; a família cobre 90% dos lotes deles e bate com o microprograma original rodando no
  interpretador da VU1 (494 mil vértices; a cor, a posição e a névoa iguais dentro do truncamento).
- O **protótipo da física** (seção 11): o corpo rígido portado, com o microcódigo da VU0 reescrito à mão, igual bit a
  bit ao código original do PS2 (o EE e a VU0) rodado nos interpretadores do port; estimativa 26-39 dias.
- O **nightfire-cxbx** (NightfireResearch; seção 12 e `docs/cxbx.md`), que porta a versão de Xbox rodando o x86
  original: os nomes deles dão 2.376 funções novas com nome ao `DRIVING.ELF` (o código do jogo de 63% para 84%) e 3.629
  pares Xbox ↔ PS2 (74% dos bytes do jogo com par de confiança >= 2), e o C++ deles da física, do mundo, do CARP, do
  `Scheduler` e do render acima do EAGL vira referência de leitura: estimativa **20-32 dias**.

## 1. O que há no disco

| Arquivo | Tamanho | O que é |
|---------|---------|---------|
| `BASE.ELF` (= `SLUS_205.79`) | 116.792 | o carregador: reinicia o IOP, sobe os IRX, carrega `ACTION.ELF` ou `DRIVING.ELF` (seção 4) |
| `DRIVING.ELF` | 2.895.244 | o executável das missões de carro; sem `.symtab`; `.text` 0x21d6fc bytes a partir de 0x107100, entrada 0x24c0d8 |
| `DRIVING/DRIVING.SYM` | 585.699 | mapa de símbolos de uma compilação anterior do `DRIVING.ELF` (seção 2) |
| `DRIVING/MISC.VIV` | 189.097.360 | BIGF sem compressão: render methods do EAGL, galerias do HUD, textos, telas de carga, os 12 filmes MPC, carros comuns |
| `DRIVING/RACE.VIV` | 8.877.214 | BIGF de uma pista a mais, `snow2a_race` (missão 8 da tabela do `main`, que o `ACTION` não pede) |
| `DRIVING/MIS<n>.VIV` | 6-10 MB cada | BIGF de cada missão (RefPack): pista, carros, personagens, animações, texturas, tuning, bancos de som |
| `DRIVING/MIS<n>.MUS` | 6-18 MB cada | BIGF de fluxos de música EA (`.asf`, EA-XA 48 kHz estéreo) |
| `DRIVING/MIS<n>EN.SPE` | 7-37 MB cada | BIGF de fluxos de fala e ambiente (`.asf`) em inglês |
| `MODULES/SNDDRV.IRX` | 99.219 | o driver de som da EA no IOP ("SND_Library_EE_Driver"), com `.symtab` (149 funções) e STABS |

As missões, pelo índice que o `ACTION` passa (fase `0x090000nn` menos `0x09000000`) e pela tabela do `main` do `DRIVING`:

| Índice | Pista (arquivo) | Missão | Quem pede |
|--------|-----------------|--------|-----------|
| 1 | `paris_mis01` (MIS01) | Paris Prelude | `ACTION` (09000001); também a "minimissão" antes do primeiro codinome |
| 2 | `uw_mis11` (MIS11) | Deep Descent (o submarino) | `ACTION` (09000002) |
| 3 | `junglea_mis13a` (MIS13A) | Island Infiltration, parte A | `ACTION` (09000003) |
| 4 | `jungleb_mis13b` (MIS13B) | parte B | o próprio `DRIVING` (`Bond_SetExitToDriving`) |
| 5 | `snow1a_mis3` (MIS3) | Alpine Escape | `ACTION` (09000005) |
| 6 | `snow2a_mis4` (MIS4) | Enemies Vanquished | `ACTION` (09000006) |
| 7 | `junglec_mis13c` (MIS13C) | parte C | o próprio `DRIVING` |
| 8 | `snow2a_race` (RACE) | uma corrida na neve | ninguém no jogo final (parâmetro `-T` de linha de comando, depuração) |
| outro | `paris_map` | (padrão do `main`) | |

## 2. O DRIVING.SYM

### Formato

Um mapa little-endian sem cabeçalho:

```c
struct SymEntry { uint32_t value; uint32_t name_offset; };   // N entradas, em ordem crescente de valor
// depois: os nomes, ASCII terminados em zero, na ordem das entradas; N = name_offset da entrada 0 / 8
```

12.719 entradas (nomes de 0x18d78 a 0x8efe3). Os nomes vêm demangled (`ActActor::Draw(RViewCamera *, bool, bool)`) e
cortados em 63 caracteres (1.430 nomes); a lista inclui, além das funções e dos dados:

- os objetos do link, com o caminho, no endereço onde começa a parte deles em cada seção
  (`d:\ToBurn\BondPS2\PS2_EE_Release\RWater.obj`, `d:\ToBurn\Library\SN\PS2\ee\lib\libpad.a(libpad.o)`); só 112 objetos
  marcam o `.text`, então não dá para atribuir toda função a um objeto pelo endereço;
- constantes do linker (`_stack_size` 0x40000, `_heap_size` -1, `_umemory_size`), `_gp`, `_fbss`, `_end`;
- os rótulos dos microprogramas da VU0 em bytes da memória de microprograma (`VU0MICRO_MultiplyMatrix` 0x110,
  `ResolveLeverForces` 0x1a0, `Pose` 0x228, `CheckAgainstVisCurtains` 0x490, `CheckPositionAgainstVisCurtains` 0x540,
  `UpdatePositionAndOrientation` 0x568: os `vcallms` do ELF chamam 0x34, 0x92, 0xa8 e 0xad, as mesmas posições / 8);
- as funções e dados do RTTI e das tabelas virtuais (`X type_info function`, `X type_info node`, `X virtual table`).

`python tools/driving_sym.py info` resume; `list`, `objs`, `modules` e `classes` listam (para `out/`).

| | Contagem |
|---|---|
| funções nomeadas no `.text` | 9.519 |
| classes com métodos | 818 (447 com `type_info`, 425 tabelas virtuais) |
| objetos distintos | 451: 208 do jogo (`BondPS2`), 39 do áudio (`BondAudioPS2`), 27 do `SupportPS2`, 60 do EAGL, 61 do REAL, o resto SN/Sony/GCC |

Os diretórios dos objetos dão a organização do projeto na EA: `BondPS2` (o jogo), `BondAudioPS2` (o som do jogo),
`SupportPS2` (CARP, sistema de arquivos `UFileLoader`, `USymbolTable`, `UPS2SpeedTalk`), `UberBondPS2` (o bloco de
troca), `PlayMPC_PS2` (filmes MPC), `BondPreProcessPS2/materials` (os materiais `libbondrmz`), `Library/EAGLPS2`
(`libeagla`, `libeaglanima`), `Library/REALPS2` (`librealfilez`, `librealmema`, `librealinputz`, `librealsystemz`,
`librealshapez`, `librealfontz`, `librealmathz`, `libsndps2z`, `librealdebugz`) e o SDK (`Library/SN/PS2/ee/lib`).

### Não é o ELF do disco

| | SYM | `DRIVING.ELF` do disco |
|---|---|---|
| data | 14/10/2002 | 18/10/2002 |
| `.text` | 0x107100-0x3824ec (2.602.988 bytes) | 0x107100-0x3247fc (2.217.724) |
| `.vutext` | 0x1790 bytes | 0x1790 bytes (igual) |
| `.data` / `.rodata` | 305 KB / 1.020 KB | 250 KB / 412 KB |
| `.bss` | 491 KB | 390 KB |
| `ENTRYPOINT` / `main` | 0x28c240 / 0x190590 | 0x24c0d8 / 0x174bf0 |

O SYM é de uma compilação maior: o `.text` 15%, o `.rodata` 2,5 vezes. A diferença se espalha pelo código todo (cresce
aos poucos, objeto a objeto), o que sugere verificações e mensagens de depuração ligadas só nela (palpite); os
subsistemas de depuração em si (`DebugMenu`, `DebugVariable<...>`, o perfilador "Sapphire", as métricas `DNightrun`)
existem nos dois. As primeiras
funções do primeiro objeto (`AnimationController::AnimationController` em 0x107338, o destrutor em 0x107358, os
`GetCurrentFrame`/`GetTotalFrames` de 12 bytes) estão no mesmo endereço nos dois, e daí a diferença cresce. Até a ordem
dos objetos da libc no link mudou. Então os nomes não podem ser aplicados pelo endereço: é preciso casar as funções.

### O casamento (`python tools/driving_sym.py match --evaluate`)

A ordem das funções no link é a mesma nas duas compilações (fora a libc). O casamento usa âncoras e alinha o resto
entre elas:

1. **Bibliotecas**: cada função da `.symtab` do `ACTION.ELF` (que tem símbolos) é procurada no `DRIVING.ELF` pelo
   padrão de bytes com os campos relocados mascarados (`jal`, `%hi`/`%lo`); só os casamentos únicos com o mesmo nome no
   SYM (246).
2. **RTTI**: a função `type_info` de uma classe (`__tf` do g++ 2.95) é a que lê a cadeia com o nome "mangled" do tipo
   (`19AnimationController`, `Q28EAGLAnim10FnAnimBank`) no `.rodata`; a de uma classe derivada também lê o nome da base,
   o que dá a hierarquia (420 de 447).
3. **Construtores e destrutores**: gravam o ponteiro da tabela virtual da classe (a tabela que aponta a `__tf` na
   segunda entrada; o ponteiro gravado é o endereço + 8), quando a classe tem tantas funções que o gravam quanto nomes
   de construtor e destrutor no SYM (472).
4. A entrada (`ENTRYPOINT` = `e_entry`) e o `main` (a única função do jogo que o `ENTRYPOINT` chama).
5. A maior cadeia crescente dessas âncoras; entre duas âncoras, as funções do SYM são alinhadas às do ELF pelo
   tamanho (programação dinâmica: cada nome cobre uma a quatro funções do ELF, as sem nome que seguem são estáticas, ou
   nenhuma, se sumiu), com a razão de tamanho local.
6. Rodadas extras: as entradas das tabelas virtuais (o nome do método de uma entrada vem de qualquer classe da família
   onde a função já tem nome, ou do único nome que todas as classes que a sobrescrevem têm no SYM) e os trechos de três
   ou mais funções seguidas com o **tamanho exatamente igual** nas duas compilações (código sem depuração idêntico)
   viram âncoras, e o alinhamento recomeça.

Resultado (8.696 inícios de função conhecidos no ELF: os 8.541 do Ghidra, os alvos de `jal` e as entradas das tabelas
virtuais):

| | Funções do SYM |
|---|---|
| âncoras (confiança 3) | 3.455: bibliotecas 235, type_info 420, construtores/destrutores 472, tamanho exato 2.319, tabelas virtuais 7, entrada e `main` |
| alinhadas com o tamanho igual ou quase (confiança 2) | 1.655 |
| só alinhadas (confiança 1, sugestão) | 2.551 |
| sem par | 1.858 (funções que a compilação final juntou, expandiu em linha ou tirou, e trechos que o alinhamento não decide) |

A precisão, medida tirando um grupo de âncoras e refazendo tudo sem ele (`--evaluate`):

| Teste | Acertos | Entre as de confiança 2 |
|---|---|---|
| sem as bibliotecas (cuja ordem mudou no link) | 52% | 98% |
| sem metade das type_info | 83% | 97% |
| sem os construtores e destrutores | 97% | 98% |
| sem metade dos trechos de tamanho exato | 99% | 100% |

Na região do código do jogo (antes do `crt0`), 3.496 das 5.157 funções que o Ghidra acha no ELF ficam com nome de
confiança 2 ou 3 (68%) e 92% com pelo menos uma sugestão. Exemplos conferidos à mão no código: `main` (lê o bloco de
troca), `SMissionManager::TallyScores(UberBond &, bool)`, `EEndMission`/`EPlayerWin`/`EPlayerLose` (gravam o
resultado), `GSystem::IsSavage` (compara o nome da pista). Uma sugestão de confiança 1 pode estar trocada com a
vizinha: a sugestão `UberBond::RunNextModule(tModule)` em 0x2ffe00 é de fato a `RunNextModule(char *)` (o carregador
de ELF, que recebe o caminho); a `(tModule)` é a seguinte, em 0x300058 (seção 4).

### No Ghidra

Um projeto headless separado dos do `ACTION.ELF` (`out/ghidra_headless`, `ghidra/`) e dos IRX (`out/ghidra_irx`):

```
NF_DATA=<checkout com os dados> tools/ghidra_driving.sh import         # DRIVING.ELF em out/ghidra_driving (1 minuto)
NF_DATA=... tools/ghidra_driving.sh functions                          # out/driving/functions.tsv e calls.tsv
python tools/driving_sym.py match                                      # out/driving/sym_map.tsv, sym_vtables.tsv
python tools/driving_sym.py ghidra out/driving/sym_map.tsv out/driving/ghidra_names.tsv
NF_DATA=... tools/ghidra_driving.sh names out/driving/ghidra_names.tsv  # aplica (ApplySymbolTable.java)
NF_DATA=... tools/ghidra_driving.sh decomp main "re:SMissionManager::.*" # out/decomp_driving/
NF_DATA=... tools/ghidra_driving.sh import-base                        # o BASE.ELF no mesmo projeto
```

`ApplySymbolTable.java` dá nome (com namespace: `SMissionManager::TallyScores`) às funções de confiança 2 e 3, cria as
que o Ghidra não achou (151), põe o nome completo do SYM e a confiança no comentário de cabeçalho, e só comenta as de
confiança 1 (2.551); as 282 tabelas virtuais casadas viram rótulos `Classe::__vt`. `ExportDecompiled.java` agora aceita
o nome com namespace. O mapa e os nomes são derivados do disco: ficam em `out/`.

## 3. Arquitetura

### Linguagem e ABI

C++ do GCC 2.95.3 (SN Systems), com RTTI e tabelas virtuais **sem "thunks"**: no `.rodata`, entradas de 8 bytes
`{s16 delta, s16 índice, ponteiro}`, a primeira nula, a segunda a `__tf`; o objeto guarda o endereço da tabela + 8.
Chamada virtual: `(*(vt + 8k + 4))(this + *(s16 *)(vt + 8k))`. Há STL (`basic_string` com `chunky_alloc`,
`_Rb_tree`, `deque`), modelos (`URefCounter<T>`, `DebugVariable<T>`) e quase nenhuma exceção (`.gcc_except_table` de
0x148 bytes). Os eventos de missão (`E*`, 891 funções) fazem o trabalho no **destrutor**: o SYM só lista construtor,
destrutor, `GetEventName` e a `type_info`, e o `~EPlayerLose` de 968 bytes é quem chama `SMissionManager::Lose`, o HUD
e grava o resultado.

### Entrada e laço principal

`ENTRYPOINT` (crt0) → `main` (Bond.obj): lê o bloco de troca em 0x100000 (seção 4), escolhe a pista pelo índice da
missão, mapeia a dificuldade (1 → 0, 2 → 1, 3 e 4 → 2), lê os argumentos (`-T<pista>`, `+`/`-` opções de depuração),
`Bond_StartUpSystem`, depois `GameLoop_MainGameLoop`:

1. `GameLoop_StartUp`: a carga da missão. Pelas cadeias e pelos nomes (o corpo não foi lido): os BIGF da missão e o
   comum (`UFileLoader::StartUsingBigFile`; `Opening %s.viv`, `cd:driving\misc.viv`, `driving\%s.spe`), a tela de
   carga (`GLoadingScreen`, com as dicas e o `data\loading\<pista>.ini`), a pista e os objetos.
2. `RunTheGame`: toca o filme de abertura (MPC com legenda `.sub`), cria as tarefas do `Scheduler` (sete tarefas, em
   dois escalonadores) e roda `while (estado != 2) { ActionQueue; entrada; Scheduler::Run }`; no fim, o filme de
   encerramento.
3. `GameLoop_CleanUp`, `GameLoop_StopUsingMainBigFile`; o `main` grava o resultado e chama
   `UberBond::RunNextModule(tModule)` (seção 4).

### Os módulos

Funções e bytes da compilação do SYM, agrupados pelo nome (`python tools/driving_sym.py modules`; aproximado: o
SYM não marca o objeto de toda função):

| Grupo | Funções | Bytes | Com nome no ELF |
|---|---:|---:|---:|
| Render e câmeras (`R*`) | 1.249 | 366.168 | 662 |
| EAGL: gráficos (`libeagla`, `libbondrmz`) | 659 | 212.832 | 62 |
| IA (`AI*`) | 607 | 204.032 | 322 |
| SDK da Sony (libkernl, libpad, libmc, libcdvd, libmpeg, libdma...) | 748 | 181.552 | 462 |
| REAL (arquivos, memória, entrada, som SND, sistema) | 824 | 177.980 | 176 |
| Áudio e filmes do jogo (`BondAudioPS2`, PlayMPC/RCMP) | 659 | 171.960 | 386 |
| Eventos de missão (`E*`) | 891 | 144.152 | 743 |
| EAGL: animação (`libeaglanima`) | 624 | 142.760 | 258 |
| Física e veículos (`P*`, `RigidBody`, `Newton`) | 337 | 139.328 | 212 |
| Mundo: colisão, estradas, gatilhos (`W*`) | 284 | 132.088 | 211 |
| Simulação, missões e armas (`S*`, mísseis, explosões) | 425 | 126.192 | 238 |
| HUD, galeria, carga, legendas (`G*`) | 322 | 114.992 | 218 |
| Jogo: outros | 381 | 97.640 | 269 |
| `SupportPS2` (`U*`, CARP, SpeedTalk) | 384 | 76.864 | 177 |
| Infraestrutura (atributos, eventos, entrada, opções, cartão) | 219 | 74.800 | 139 |
| Personagens e animação (`Act*`) | 312 | 71.672 | 181 |
| STL, modelos e construtores globais | 327 | 70.464 | 250 |
| Depuração, ajuste e perfil (`D*`) | 100 | 23.880 | 51 |
| C/C++ do GCC (libc, libm, libgcc) | 167 | 21.960 | 93 |
| **total** | **9.519** | **2.551.316** | **5.110** |

O que cada parte faz, pelos nomes (as classes com mais métodos entre parênteses):

- **Render** (`R*`, sobre o EAGL): `RRenderer`, `RRenderWorld` e `RRenderWorldCulling` (cortinas de visibilidade,
  `WVisCurtain`, testadas no microcódigo da VU0), `RSceneObj` (105), `RVehicle`, `RSky`, `RWater`, `RFog`,
  `RShadowMap`, `RReflection` (38), `RGlare_PS2`, `RLensFlare`, `RLightning`, `RLightManager` (43), `RParticle`/
  `RParticle_PS2`, `RDebris` (94 KB), `RVehicleParticle`, `RTyreTrack_PS2` (marcas de pneu), `RDecal`, `RBlastWave`,
  `RBulletStreak`, `REmp`/`REmpBolts`, `RPostProcessing`, `RColorize`, `ROffscreenBuffer`, `RTextureContextManager`.
- **Câmeras**: `RPlayerCamera` (112), `RPlayerCamState`, `RPlayerViewCamera`, `RWorldCamera`, `RCameraSpline`,
  `RCameraMath`, `RCameraIniLoader` (`camera.ini`), `RDirectorQueue` (cenas), `RPIPView`/`RPIPCamera` (imagem na
  imagem), `RMissileCam`, `RSniperZoom`; os pontos de vista do jogador vêm dos `povcfg<n>.def`.
- **Física**: `PVehicle`, `PBondCar` (119), `PHelicopter`, `PTank`, `PhysicsObject` (52), `RigidBody` (56),
  `SimpleRigidBody`, `PackedRigidBody`, `Newton`, `OBB`, com o passo de integração e as forças de alavanca no
  microcódigo da VU0 (`Timestep_LoadAndStartVU0`, `RigidBody::ResolveLeverForces`); colisão em `WCollisionMgr` (39),
  `WCollider`, `WCollision`, `WGrid`/`WGridNode`; parâmetros nos `.tun` (`GRAVITY=-34`, atrito, restituição) e `.atr`.
- **IA**: `AIElementController` (o gerente, 110 KB), `AIVehicleController`, `AIGroundVehicle` (114), `AIHelicopter` e
  `AIHeliScript`, `AIRoadSpawn` com `WRoadNetwork`/`WRoadNav` (o tráfego), `AISplinePath`, `AIZone`, e os personagens a
  pé: `AICharacter` (74) com `AICharacterEnemyGround/Snow/SSnow/Sunroof/Window`, `AICharacterEnemyDriver`,
  `AICharacterPassenger`, `AICharacterPedestrian`, `AICharacterBond`, `AICharacterHands`.
- **Personagens**: `ActActor`, `ActCharacter`, `ActIK`, `ActPoser`, bancos de animação (`ActAnimationDatabase`,
  `ActAnimGroup`) sobre o `libeaglanima` (`FnRunBlender`, `FnPoseBlender`, canais delta/quaternion comprimidos), com o
  microcódigo `VU0_charAnim`.
- **Missões**: `SMissionManager` (91), `SMissionRule`, `Simulation` (64), `SWeaponManager` (88 KB), mísseis, granadas,
  minas, `RayShell`, `Explosion`, `Sentry`, `Smackable` (objetos quebráveis), os eventos `E*` (`EFireWeapon`,
  `ESpawnVehicle`, `EStartTimer`, `EPassObjective`...), `WTriggerManager`, `ActionQueue`, `AttributeSystem`/
  `AttributeSet` (os `.atr`), `DTuningFile` (os `.tun`), `FeatureManager`, `Replay`.
- **HUD e telas**: `GHud` (107: o HUD, o menu de pausa `DrawPauseMenu`, `SetMissionFailed`, `TriggerBloodCurtain`),
  `GFX`/`GFXGallery`/`GGallery` (as galerias `.gal`), `GLoadingScreen`, `GSubtitles`, `GLocale` (`.loc`), `GSystem`.
- **Som**: o jogo (`BondAudioPS2`: `AMix`, `ABank`, `AStream`, `AVoice`, `AFader`, `AVehicle` (62), `ATrafficVehicle`,
  `ASubmersible`, `ASnowMobile`, `AUltraLite`, `AHelicopter`, `ASentry`, `ASoundManager`...) sobre o SND da EA
  (`libsndps2z`, decodificador `SND::CEAXABLKDecf` de EA-XA) e o `SNDDRV.IRX`, um mixador em software no IOP
  ("SPU thread", `mix/*.c`). O `SFX.IRX` (EuroSound) do `ACTION` não é usado.
- **Arquivos**: `librealfilez` (`locatbig` acha arquivos dentro dos BIGF, `cddrv`, `hlafile`/`hlsfile` assíncronos,
  `stream` para os fluxos de áudio), `UFileLoader` (`StartUsingBigFile`, lista de pedidos), RefPack (`unpackz`,
  `REF_decode`).
- **Filmes**: `PlayMPC` com o RCMP (`rcmp_ps2_sony_codec`: o `libmpeg`/IPU da Sony) e um áudio próprio
  (`RCMP::AUDIO_PLAYER`); os 12 `.mpc` do `MISC.VIV`.
- **Entrada**: `IOModule`, `InputDevicePS2`, `InputConfigMgr` (`Master.def`, `FrontEnd.def`, `drivecfg.def`), o
  `librealinputz` (teclado, controle, mouse de desenvolvimento), `IFeedback` (vibração).

### Render: o EAGL e os "render methods"

O `.vutext` do `DRIVING.ELF` tem só 0x1790 bytes: três programas da **VU0** enviados por DMA (`VU0_charAnim_dma_start`,
`VU0_physics_dma_start`, `VU0_culling_dma_start`; quatro overlays `.DVP`). Não há microcódigo da VU1 no ELF e nada do
microcódigo da Eurocom (o do `ACTION`, `docs/formats.md` "Microprograma da VU1") é usado. A VU1 roda os **render
methods** do EAGL 4.06.01, que vêm nos dados como objetos ELF relocáveis (`MISC.VIV`):

- `data\render\eaglrm.o`: 53 métodos genéricos (`Gouraud`, `LitTexture`, `LitTextureEnvmap`, `ClipLitTextureEyeEnvmap_Skin`,
  `ArbitraryShadow_Skin`, `Texture_ToonShade2_Skin`, `DepthOfField`, `XFadeScrollTexture`...);
- `data\render\bondrm.o`: 62 do jogo (`MatCarTexture`, `MatCarChrome`, `MatCarGlass`, `MatBondWater`, `BondUnderWater`,
  `BondKelp`, `MatTyreTracks`, `MatShadowMap`, `PartMaticSprites`, `MatHudTexture`...);

cada um com o seu bloco `<Nome>0__EAGLMicroCode`. Os **modelos** também são objetos ELF do EAGL (`.o`, ou partidos em
`.dat` + `.rel`): uma seção `.data` com os pacotes VIF/GIF e as relocações, e símbolos com tipo no nome que o
`EAGL::DynamicLoader` liga na carga (`__Model:::c_bondc1`, `__EAGL::TAR:::tar_tttt_22666`,
`__EAGL::GeoPrimState:::RUNTIME_ALLOC::UID=583;SetAlphaBlendMode=EAGL::ABM_BLEND;...`, o render method
`BondLitTextureAndShadow_Skin` e o microcódigo dele, as matrizes `EAGL::ViewPort::gpModelViewProjectionMatrix`). Ou
seja: o formato de malha é o pacote de DMA de cada microprograma. O port precisa entender os 115 microprogramas (são
famílias: com e sem recorte, luz, pele, envmap, sombra) e escrever um shader para cada família, como foi feito para as
quatro rotinas da VU1 do `ACTION`, só que em escala maior.

### Bibliotecas em comum com o ACTION.ELF

Procurando cada função da `.symtab` do `ACTION.ELF` no `DRIVING.ELF` pelo padrão de bytes mascarado: 409 achadas, que
são o SDK da Sony (116 `sce*`: libkernl, libpad, libmc, libcdvd, libmtap, libdma, libgraph, libipu, libmpeg) e o
runtime do GCC/newlib (243: libc, libm, libgcc), uns 106 KB; o resto são falsos positivos de funções minúsculas. As
versões do SDK batem (`PsIIlibcdvd 2420`, `libdma 2400`, `libgraph 2400`, `libipu 2400`, `libkernl 2430`, `libmc 2430`,
`libmpeg 2420`, `libmtap 2400`, `libpad 2410`); o `ACTION` liga também o `libsdr`, o `DRIVING` o `libpc`. Nenhum código
de motor é comum.

Uso da VU0 (contagem nas instruções do `.text`):

| | `ACTION.ELF` | `DRIVING.ELF` |
|---|---:|---:|
| funções | 3.038 | ~8.541 |
| instruções VU0 em macro (COP2) | 1.718 em 248 funções | 7.281 em 446 funções |
| `lqc2`/`sqc2` | 2.639 | 6.299 |
| chamadas de microprograma (`vcallms`) | 6 | 5 (física: `Timestep_LoadAndStartVU0`, `RigidBody::ResolveLeverForces`; visibilidade: `AddCurtain`, `IsVisibleAgainstCurtains`), mais o `vcallmsr` do `sceDevVu0Exec` |
| funções com MMI | 84 | 112 |

## 4. A troca entre os ELFs

### Ida: ACTION → BASE → DRIVING

1. No `ACTION`, o mapa das missões pede uma fase `09xxxxxx`; `GameFlow_Main` chega a `GF_LAUNCH_DRIVING` (9),
   `Boot_GetPTPData` (`0x001c8198`) preenche o bloco `PTPDATA` (0x31c090, 0xA50 bytes) e `psiLaunchDriving`
   (`0x00120af0`) desliga as interrupções, copia o bloco para **0x100000**, grava 1 em 0x10097c e chama
   `RunNextModule("cdrom0:\BASE.ELF;1")` (não volta).
2. O `BASE.ELF` (carregado em 0xa00000, "Welcome to BASE (Final)") reinicia o IOP com `IOPRP243.IMG`, sobe `SIO2MAN`,
   `MTAPMAN`, `PADMAN`, `LIBSD`, `SDRDRV`, `MCMAN`, `MCSERV` (e o `SNPROFIL` em desenvolvimento) e o driver de som do
   destino: **`SFX.IRX` para o `ACTION`, `SNDDRV.IRX` para o `DRIVING`** (pelo campo +0x97c). Preenche a RAM de 0x104000
   até 0xa00000 com 0x0d (o bloco de 16 KB em 0x100000 sobrevive), confere o bloco ("Verify and clear") e carrega o ELF
   com um carregador próprio (`sceOpen`/`sceRead` dos segmentos, termina as threads, `FlushCache`, salta para a
   entrada): `cdrom0:\ACTION.ELF;1` na primeira vez (o byte +0x4b4, "loader present", ainda zerado) ou quando +0x97c = 0,
   `cdrom0:\DRIVING.ELF;1` quando +0x97c = 1. Em desenvolvimento os caminhos são `host0:BondPS2_Release.elf` (o
   `DRIVING`), `host0:TestPS2_Release.elf` e `host0:BasePS2_Release.elf`.
3. O `DRIVING` lê o bloco no `main` (seção 3).

### O bloco (UberBond, 0x100000)

Os campos que um ou outro lado usa (deslocamentos no bloco; "A" = o `ACTION`, "D" = o `DRIVING`):

| Deslocamento | Quem | Conteúdo |
|---|---|---|
| +0x000 | A | o codinome (`ls.name`, 0x20 bytes); com ele não nulo, a volta trata o bloco como o perfil lido (`LS_LoadFromBuffer`) |
| +0x080, +0x084 | A | `ls.slot`, `ls.can_save` |
| +0x088..+0x0a8 | A | bytes do `PlayerSetting[0]` (agachar, mira manual, mira automática, troca automática, centralização, zoom...) |
| +0x0a4 | A | a língua do som (`SFXGetLanguage`) |
| +0x0ac, +0x0b8, +0x0bc | A | o estilo do controle, a mira automática do multijogador, `DrawInfo` |
| +0x0b0..+0x0e4 | A | `MPSettings` (o multijogador, só guardado) |
| +0x0e8, +0x0ec | A | volumes da música e dos efeitos (os do `SFX*`) |
| +0x0f0 | A | `def_codename[1]` (0x38 bytes, o perfil padrão) |
| +0x128..+0x150 | A | `menu_unlock_everything`, `CheatInfo`, o estado do jogo (`Menu_GetNightfireStatus`), os 64 bits de bônus, a dificuldade, `ls.legal_once`, a fase (`GameState` +8), `GameState[0x52]` |
| +0x154, +0x184 | A | 12 + 12 `u32` das tabelas de pontuação das missões de carro (`PlrStats_GetDrivingData`: o recorde e a linha 0) |
| +0x4b4 | BASE, D | "loader present" (`UberBond::IsLoaderPresent`) |
| +0x4f4 | D | o nome da pista (16 caracteres) |
| +0x504..+0x630 | D | estado do `DRIVING` que atravessa as execuções (as estatísticas que `TallyScores` soma, as tentativas e reinícios que `~EPlayerLose` conta); o `ACTION` devolve a área intacta na próxima ida |
| +0x540 | D | o arquivo de textos (`IDL_ENG.LOC`) |
| +0x960 | A, D | 1 = bloco válido |
| +0x964, +0x968 | A, D | volumes dos efeitos e da música / 2 (100 se o `DRIVING` rodar sozinho) |
| +0x970 | A, D | tela larga (`DrawInfo[1]`) |
| +0x974 | A, D | o índice da missão (a fase - 0x09000000; tabela da seção 1) |
| +0x978 | D → A | o resultado: 0 cumprida, 1 fracassada, 2 abandonada (com os bits 0x80/0x100 de +0x9a0), 3 (vira 1), 4 morte |
| +0x97c | D → BASE | o próximo módulo: 0 o `ACTION`, 1 o `DRIVING` de novo |
| +0x984 | A, D | a dificuldade (forçada a 1 na minimissão de Paris) |
| +0x988 | A, D | a língua dos textos |
| +0x98c..+0x99c | A, D | estilo do controle, índice do controle, vibração, HUD sempre visível, mira |
| +0x9a0 | A, D | bits: 8 = ouro em Paris (`Menu_HasMedal(0x9000001, 3)`); 0x80/0x100 fazem o `main` devolver 2 (provavelmente sair pela pausa; quem os liga não foi lido) |
| +0x9a8..+0xa10 | D → A | as 9 linhas da pontuação, {valor, alvo, pontos} cada (`NewScoresRef` = `PTPDATA` + 0x9a8, lido por `PlrStat_GetScore`) |
| +0xa14 | A, D | `PlayerSetting[0]` +0x10, o estilo de controle do carro (`Boot_GetPTPData` grava, `Boot_LoadPTPData` devolve; nome do nightfire-cxbx, `drivingConfig`) |
| +0xa18 | A, D | 0 na minimissão de Paris (antes do primeiro codinome) |
| +0xa1c, +0xa20 | A, BASE | tipo de TV (`psiGetTVType`), modo (2; o `BASE` escolhe CD ou `host0:` por ele) |
| +0xa24..+0xa48 | A, D | `PlayerSetting` +0 (inverter), modo do som, `DrawInfo[2]` (legendas), posição da tela, a melhor medalha da missão, a mira automática, a mira manual e a troca automática de arma |

### Volta: DRIVING → BASE → ACTION

- No fim da missão, o evento grava o resultado: `~EPlayerWin` grava 0 e mostra "missão cumprida" no HUD,
  `~EEndMission` grava 0 e chama `SMissionManager::TallyScores(UberBond&, true)` (soma as estatísticas e preenche
  +0x9a8); `~EPlayerLose` grava 1, ou 4 se a vida chegou a zero (ou reinicia a missão, `ERestart`, enquanto houver
  tentativas).
- O `main` acerta o resultado (3 → 1, abandono → 2, `ClearScoreBuffer`) e chama `UberBond::RunNextModule(tModule)`
  (0x300058): grava o módulo em +0x97c e carrega o `BASE.ELF` (`RunNextModule(char *)`, 0x2ffe00, o mesmo carregador de
  ELF do `BASE`). Com `Bond_SetExitToDriving(missão, pista, ...)` ativo (as partes da Ilha), grava a missão e a pista
  seguintes e pede o `DRIVING` de novo (+0x97c = 1).
- O `ACTION` reinicia do zero: `GF_BOOT` → `Boot_LoadPTPData` (`0x001c8560`) copia 0x1000 bytes de 0x100000
  (`psiGetDrivingData`), restaura o perfil, as opções, os volumes, a língua, e decide pelo resultado:

| +0x978 | Estado da missão | Página do front end |
|---|---|---|
| 0 | 6 (cumprida; `PlrStat_GetScore(0)` lê as linhas de +0x9a8) | 0x40000036, os resultados |
| 1, 2 | 2 (fracassada) | 0x4000001c, o mapa |
| 4 | 3 (morte) | 0x4000001c, o mapa |
| 3 | 2 | (nenhuma) |
| minimissão de Paris (+0xa18 = 0) | | 0x40000043 (o clipe da Esthero e o resto da introdução) |

  e pede o front end (`ResetMap_LevelToLoad(0x7000048)`).

No PC, hoje `psiLaunchDriving` é um stub e `Menu_PortDrivingSkipped` dá a missão como feita sem medalha.

## 5. Os dados

`python tools/driving_data.py stats` lê os 23 arquivos (529 MB no disco, 590 MB descomprimidos; o RefPack de todas as
entradas confere com o tamanho do cabeçalho):

| Tipo | Entradas | Bytes (descomprimidos) |
|---|---:|---:|
| SCHl (fluxos de áudio, `.asf`) | 236 | 267.856.776 |
| MPCh (filmes, `.mpc`) | 12 | 178.408.996 |
| CARP (pistas, carros, armas, `.crp`) | 89 | 58.923.776 |
| SHPS (texturas, `.ssh`) | 218 | 45.625.168 |
| BNKl (bancos de som, `.bnk`) | 56 | 15.757.152 |
| ELF do EAGL (`.o`, `.dat`) e cabeçalhos (`.rel`) | 236 + 162 | 11.068.694 |
| GGAL (galerias, `.gal`) | 12 | 9.869.824 |
| texto (`.atr`, `.tun`, `.def`, `.ini`, `.h`, `.sub`, `.dir`, `.txt`) | 442 | 1.297.302 |
| LOCH (textos, `.loc`), FNTS (fontes, `.sfn`), outros | 9 + 11 + 16 | 917.506 |

Os formatos:

- **BIGF** (`.VIV`, `.MUS`, `.SPE`): `"BIGF"`, u32 LE o tamanho do arquivo (no `MISC.VIV` o campo não bate: lido como
  big-endian dá 2.407 bytes a menos), u32 BE o número de entradas, u32 BE o tamanho do cabeçalho; entradas
  `{u32 BE posição, u32 BE tamanho, nome ASCII com zero}` (`data\track\paris_mis01.crp`); posições alinhadas a 0x800
  nos `.VIV`; os `.MUS`/`.SPE` terminam o cabeçalho com `"L225"`.
- **RefPack** (QFS da EA): `0x10 0xFB` + o tamanho descomprimido em 24 bits big-endian, depois comandos de literais e
  cópias (o `unpackz`/`REF_decode` do REAL); quase tudo nos `.VIV` de missão, nada no `MISC.VIV` nem no áudio.
- **Objetos ELF do EAGL**: ELF32 relocável MIPS (`e_type` 1) com `.data`, `.rel.data`, `.symtab`, `.strtab` e símbolos
  `__<Tipo>:::<nome>`. Os `.o` são inteiros (render methods, esqueletos `Nskel.o` com `__Bone:::Root.RToe`...); os pares
  `.dat` + `.rel` são o mesmo ELF partido: o `.dat` tem o cabeçalho e a `.data` (o que fica na memória), o `.rel` as
  tabelas de seções e de símbolos (personagens `c_bondc1`, `h_pmask1`, armas, bancos de animação
  `__AnimationBank:::driving`). Também dentro dos CARP (`ELFd`/`ELFr`).
- **CARP** (`.crp`, `"PRAC"` = `'CARP'` little-endian): um UGroup serializado da EA (seção 10, `src/driving/carp.h`):
  grupos de 16 bytes `{FourCC gravado ao contrário, u32 bandeiras, u32 contagem, u32 posição}` cujo vetor tem
  `bandeiras >> 5` subgrupos e `contagem` dados `{etiqueta, bandeiras (tamanho = >> 8), contagem, posição}`; as
  famílias numeradas têm etiqueta curta (duas letras e um u16, bit 0). Um carro (`vanquish.crp`): a raiz com `ELFd`,
  `ELFr` (o modelo EAGL), `Sect` e `sn#0000` (o `.ssh`) e os grupos `Arti` (280 dados: `Base`, `Cams`, `Name`, `Skel`,
  as instâncias `in`, as peças `as`, os nomes `sr`, as animações `an`/`aq`, `el`, `ef`, as listas por vista `vd`) e
  `Shar` (vazio). Uma pista (`paris_mis01.crp`, 6,8 MB, 499 grupos e 15.490 dados): 494 objetos `Arti` (`Base`, `Name`,
  `sr`, `as`, `in`, `ca`...), `CDat` (`CGrd`, a grade de colisão, e 2.111 `cn`), `Map` (`Wmap`, `AICo`/`AIEl`/`AISp` da
  IA, `Trgr` (gatilhos), `Rule` (regras da missão), `MSet`, `Cams`, `Audi`, `Envi`, `Shad`, `VisC` (cortinas de
  visibilidade), 868 `el`, 2.787 `sr`), `RNgp` (a rede de estradas: `RNhd`, `rn`, `rs`, `rr`, `ri`, `rj`) e, na raiz,
  `MapN`, `Sect` e o `ELFd`/`ELFr`. O conteúdo dos blocos do mundo não foi decodificado.
- **SHPS** (`.ssh`, a textura "shape" da EA para PS2): `"SHPS"`, u32 tamanho, u32 imagens, versão (`G354`, `G344`),
  diretório `{nome de 4 letras, u32 posição}`; cada imagem é uma cadeia de blocos `{u8 tipo, u24 até o próximo,
  u16 largura, u16 altura, ...}`: 0x02 8 bits + 0x21 paleta de 32 bits, 0x05 RGBA 32, 0x04 RGB 24, 0x69 metadados,
  0x70 o nome longo.
- **SCHl** (`.asf`): fluxo da EA, blocos `SCHl` (cabeçalho "PT", plataforma 5 = PS2), `SCCl`, `SCDl` (dados), `SCEl`;
  campos PT `0x82` canais, `0x84` taxa, `0x85` amostras, `0xa0` codec = 0x0A, **EA-XA** (ADPCM da EA). Música e falas a
  48 kHz estéreo (`all.asf` de Paris: 126,6 s; `city.asf`: 150,3 s).
- **BNKl** (`.bnk`): bancos do SND da EA com um cabeçalho PT por som (16 kHz mono no `g_common.bnk`; codec padrão da
  plataforma, provavelmente PS-ADPCM: a conferir); os `.h` ao lado são os `#define SFX_<nome> <índice>`.
- **GGAL** (`.gal`, `"LAGG"`): as galerias do HUD e das telas (`hudNTSC.gal`, 1 MB; `gall1`, `ears_gall1`, `sav_gall1`),
  blocos `GANM`...; **LOCH**/`LOCI` (`.loc`): os textos por língua (9); **FNTS** (`.sfn`): fontes.
- **MPCh** (`.mpc`): o filme MPC da EA: um vídeo MPEG-2 elementar ("Encoded with MPEG Gimex module") e o áudio da EA;
  12 filmes de missão (`paris_intro`, `paris2`, `alps_intro`, `snow_intro`, `island_intro2`, `ja_intro`, ...) com as
  legendas em `.sub` (`FRAME_640=8706`: quadro e rótulo).
- **Texto**: `.atr` (atributos em INI: `[sentry] BULLET_STREAK_TYPE=6`), `.tun` (tuning: física, render, neblina, luz
  por pista), `.def` (controles: `Master.def` lista os pontos de vista, `drivecfg.def`), `.ini` (câmera, áudio, telas
  de carga), `.dir` (`attrib.dir`), `.sub`.

Comandos (a saída é dado do jogo: fica em `out/`):

```
python tools/driving_data.py list MIS01.VIV                     # entradas, comprimidas ou não, tipo
python tools/driving_data.py extract MIS01.VIV out/driving/files/MIS01 vanquish
python tools/driving_data.py ssh MIS01.VIV:vanquish.ssh          # imagens
python tools/driving_data.py carp MIS01.VIV:paris_mis01.crp      # blocos do CARP
python tools/driving_data.py asf MIS01.MUS:all.asf               # cabeçalho PT do fluxo
python tools/driving_data.py eagl MIS01.VIV:c_bondc1.dat         # símbolos de um objeto do EAGL
```

## 6. Tamanho e plano

### O tamanho

O código do jogo no ELF (do início do `.text` ao `crt0`) tem 1.331.152 bytes e ~5.150 funções; as bibliotecas, mais
886.572 bytes e ~3.380 funções. Para comparar: o `.text` inteiro do `ACTION.ELF` tem 1.288.996 bytes e 3.038 funções,
das quais o port decompilou 2.103 (79% dos bytes) em cerca de duas semanas de calendário e 222 commits, mais a camada de
plataforma (72 mil linhas no `src/engine`, 28 mil em `src/platform`, `src/render` e `src/ui`). As funções do `DRIVING`
são menores (C++) e mais numerosas; o SYM dá o nome e os tipos dos parâmetros (até o corte de 63 caracteres) de 68% das
funções do jogo, mas nenhuma estrutura (não há STABS).

### O que o port reaproveita

| Peça do port | Reaproveita? |
|---|---|
| Janela, laço de vídeo, entrada (`psi_input`, XInput, teclado e mouse, `[bindings]`) | sim: as ações do `InputConfigMgr` mapeadas sobre os mesmos dispositivos |
| Renderer D3D11 (`src/render`), interpolação entre ticks, escala de render, MSAA, captura e `--bench` | sim a infraestrutura; não os shaders (o EAGL tem outros microprogramas e outro formato de malha) |
| Menus Modern (`src/ui`), HUD em alta definição | sim o kit e o desenho; as telas do `DRIVING` (pausa, missão fracassada) são outras |
| SPU2 em software, saída XAudio2 (`spu2`, `audio_out`) | sim; o driver é outro (`SNDDRV.IRX`, com símbolos, ou o SND reimplementado) e falta o EA-XA |
| Filmes (Media Foundation MPEG-2, `psi_movie`) | sim o decodificador de vídeo; falta o contêiner MPC e o áudio da EA |
| Interpretador MIPS (`mips_interp`), `tools/vudis.py` | sim, para construtores estáticos e para ler o microcódigo |
| Cartão de memória, perfil, front end, pontuação | sim, do lado do `ACTION` (o bloco de troca já tem o leitor, `Boot_LoadPTPData`) |
| `FILES.BIN`, `LoaderLoad`, o motor da Eurocom | não |

### As fases

0. **Infraestrutura** (o `DRIVING` como um segundo "módulo" do port). Biblioteca `nf_driving` (CMake), leitores C++ de
   BIGF/RefPack/SHPS/CARP e do ELF do EAGL (o `DynamicLoader`: relocação da `.data`, resolução dos símbolos com tipo),
   texturas SHPS para o renderer, um executável de teste `nf_driving_check` que carrega as 8 pistas e todos os carros
   e personagens (como o `nf_loader_check`), e a revisão manual dos nomes de confiança 1 nos módulos de cada fase.
1. **Um carro na pista com câmera** (`--track paris_mis01`). O mundo do CARP (seções, objetos, cortinas de
   visibilidade, céu, neblina e luz dos `.tun`) desenhado com os primeiros render methods (`MatBondTexture*`,
   `ObjTexture*`, `MatCarTexture*`, `MatCarGlass`, `Gouraud`); a física (`PVehicle`, `PBondCar`, `RigidBody`,
   `PhysicsObject`, `Newton`, `WCollisionMgr`, `WGrid`) com o microcódigo da VU0 (passo, forças de alavanca, posição e
   orientação) portado e testado contra o original; a entrada (`InputConfigMgr`, `drivecfg.def`); `RPlayerCamera`; o
   `Scheduler` e o `GameLoop`. Critério: o Vanquish anda em Paris com a câmera de perseguição, a 60 fps num iGPU.
2. **Mundo vivo e som.** Tráfego (`AIRoadSpawn`, `WRoadNetwork`, `AIVehicleController`, `AIGroundVehicle`), objetos
   quebráveis (`Smackable`), partículas, marcas de pneu, reflexos, brilho, sombras, água; personagens (`libeaglanima`,
   `Act*`, `VU0_charAnim`); som (o SND: decodificador EA-XA, os bancos BNKl, o mixador do `SNDDRV.IRX` portado como o
   `SFX.IRX` foi, e o `BondAudioPS2`: motor do carro, música interativa, falas e ambiente).
3. **Missões completas.** `SMissionManager`, regras e gatilhos do CARP, os eventos `E*`, `ActionQueue`, armas
   (`SWeaponManager`, mísseis, `RayShell`, explosões), inimigos (`AICharacter*`, `AIHelicopter`, `Sentry`), os veículos
   especiais que o código tem (submarino `ASubmersible`, moto de neve `ASnowMobile`, ultraleve `AUltraLite`, tanque
   `PTank`, helicóptero `PHelicopter`), HUD (`GHud`, as galerias `.gal`), textos (`.loc`),
   legendas, os filmes MPC, a pausa e a missão fracassada, `TallyScores` e o resultado. As sete pistas jogáveis do
   começo ao fim.
4. **Integração e PC.** A troca com o `ACTION` pelo bloco (o `psiLaunchDriving` de verdade e a volta pelo
   `Boot_LoadPTPData`), as três partes da Ilha encadeadas, as opções compartilhadas (`nightfire.ini`), as telas no
   estilo Modern, a interpolação entre ticks, a resolução e o aspecto livres, o `--bench` no iGPU.

### Um processo ou dois

O PS2 troca de ELF e começa do zero a cada troca (até entre as partes da Ilha); o código decompilado do `DRIVING`
supõe isso: milhares de globais e construtores estáticos que valem só para uma execução, endereços fixos (o bloco em
0x100000, umas 50 funções o acessam direto).

| | Dois processos (`nightfire.exe --module driving <bloco>`, o mesmo executável) | Um processo |
|---|---|---|
| Estado global limpo a cada troca | de graça, como no PS2 | só com cuidado: uma DLL (`driving.dll`) carregada e descarregada a cada missão dá globais novas; código ligado estaticamente exige reinicializar tudo à mão |
| Conflito de nomes entre os dois motores | nenhum | `namespace` para o código do `DRIVING` (`Mat_*`, heap, `Rand`...) |
| Troca de janela, dispositivo D3D, áudio, tela cheia exclusiva | janela nova (é preciso esconder a antiga só depois do primeiro quadro da nova); tela cheia exclusiva troca de modo duas vezes | a mesma janela e o mesmo dispositivo, transição sem piscar |
| Memória (processo de 32 bits) | um heap por processo | os dois heaps no mesmo espaço (cabe, com `/LARGEADDRESSAWARE`) |
| A volta ao `ACTION` | o `ACTION` recomeça pelo `GF_BOOT` com o bloco, exatamente como no PS2 | o `ACTION` precisa de um "reinício quente" pelo `GF_BOOT` (hoje o port não reinicia o motor inteiro) |
| Depuração e testes | cada lado isolado; o `--module driving` serve de ferramenta de desenvolvimento | um só depurador |

Recomendação: desenvolver as fases 0 a 3 com o `DRIVING` numa biblioteca estática e o seu próprio executável de
desenvolvimento, e na fase 4 escolher entre `driving.dll` (um processo, globais novas a cada carga, serviços do
hospedeiro por interface: janela, dispositivo, áudio, entrada, configurações) e o mesmo executável em segundo processo
(mais simples e fiel; uma transição visível de uns segundos, aceitável, como a tela de carga do PS2). A DLL é o melhor
resultado para o jogador; o segundo processo é o caminho de menor risco se a interface ficar grande.

### Estimativa

Contando funções: ~5.150 do jogo e umas 2.000 das bibliotecas da EA que precisam de porte (o EAGL que interpreta os
modelos, a animação, o SND, o CARP/Support e o áudio do jogo; o REAL e o SDK viram a plataforma do PC), contra as
~2.100 do `ACTION` já portadas. Por fase, no ritmo deste repositório e com incerteza alta (±50%):

| Fase | Estimativa |
|---|---|
| 0. Infraestrutura | 2-3 dias |
| 1. Carro na pista | 6-10 dias |
| 2. Mundo vivo e som | 8-12 dias |
| 3. Missões completas | 10-15 dias |
| 4. Integração e PC | 3-5 dias |
| **total** | **30-45 dias**: duas a três vezes o porte do `ACTION.ELF` feito até aqui |

O que mais pode estourar a estimativa: o render (seção 7) e a fidelidade da física. Com a versão de Xbox como oráculo da
física (seção 9, "Efeito no plano") as fases 1 a 3 encolhem um pouco: 27-41 dias no total.

## 7. Riscos

1. **Os nomes não são exatos.** O SYM é de outra compilação: 32% das funções do jogo ficam sem nome confiável e não há
   tipos de estruturas. A decompilação de C++ do GCC 2.95 (tabelas virtuais com delta, RTTI, STL com `chunky_alloc`,
   eventos que trabalham no destrutor) é mais lenta que a do C da Eurocom.
2. **O render.** 115 microprogramas da VU1 em dados (`eaglrm.o`, `bondrm.o`) e modelos que são pacotes de DMA para
   eles: cada família precisa ser lida (`tools/vudis.py --eagl` já desmonta o microprograma de um método) e virar
   shader; o EAGL monta pacotes em tempo de execução (`TAR`, `GeoPrim`, `DmaMgr`, `RenderContext`), e efeitos como reflexo, brilho,
   sombra e profundidade de campo usam truques do GS (render-to-texture, `ROffscreenBuffer`, `RPostProcessing`).
3. **A física na VU0.** O passo dos corpos rígidos e a visibilidade rodam em microcódigo da VU0 (`vcallms`), e 446
   funções usam a VU0 em macro (4 vezes mais instruções que o `ACTION`): máscaras de destino, a aritmética da VU (sem
   denormais nem infinitos, arredondamento), e uma física de carro que precisa reproduzir o comportamento exato para
   que a direção e os pulos fiquem iguais. O ritmo da simulação (o `Scheduler`, as tarefas por quadro) ainda não foi lido.
   Mitigado pela versão de Xbox (seção 9): o algoritmo de cada passo está lá em C++; o passo da simulação é 1/60 s.
   Resolvido no núcleo (seção 11): o passo e as alavancas portados e iguais ao PS2 bit a bit.
4. **Streaming.** O sistema de arquivos assíncrono do REAL (`hlafile`, `stream`, BIGF por missão) e os fluxos de áudio
   e filmes: a lógica pode depender do tempo de carga (telas de carga, eventos esperando um fluxo).
5. **Outra pilha de som.** O SND da EA (EE e `SNDDRV.IRX`, um mixador em software no IOP), EA-XA, bancos BNKl;
   o `SNDDRV.IRX` tem símbolos e STABS, o que ajuda, mas é um segundo driver inteiro.
6. **Formatos não decodificados.** Os blocos do CARP (o mundo, a rede de estradas, as regras), as galerias `.gal` (o
   HUD), os `.loc`, as fontes `.sfn`, o contêiner MPC.
7. **A troca.** O bloco em endereço fixo, os campos que atravessam as execuções (+0x504..+0x630), o encadeamento da Ilha,
   e o reinício do `ACTION` na volta.
8. **Dois motores em 32 bits.** Memória, conflito de nomes e estado global (seção 6, "Um processo ou dois").
9. **As duas compilações diferem.** 1.858 nomes do SYM ficam sem função no ELF, o código de uma mesma função muda de
   tamanho entre as duas, e nomes de confiança 1 podem estar deslocados para a função vizinha.

## 8. Recomendação

Vale fazer, mas como um projeto do tamanho do porte do `ACTION.ELF` (ou maior), e começando pelo que reduz o risco:
primeiro ler e reproduzir **um** render method e **uma** malha de carro do EAGL (um protótipo da fase 0/1 que desenha o
Vanquish girando, com o carregador do ELF do EAGL e o SHPS), e portar o microcódigo da VU0 da física com testes; só
depois decidir o cronograma inteiro. Até lá, o pulo atual (`Menu_PortDrivingSkipped`) continua sendo o comportamento
certo do port. Os nomes do Ghidra já valem para qualquer leitura do `DRIVING.ELF`, e o leitor de dados e o mapa do
bloco de troca estão prontos para a fase 0.

O plano de longo prazo (a "007 Anthology", CLAUDE.md) reforça a separação: o `DRIVING` seria o primeiro cliente da camada
de PC reaproveitável que não é da Eurocom (janela, entrada, D3D11, SPU2, filmes, menus Modern), o que favorece o módulo
à parte (DLL ou segundo processo) em vez de misturar os dois motores. E, palpite a conferir, se outros jogos de Bond da EA
da mesma época (Agent Under Fire, 2001, também tem missões de carro) usam o mesmo EAGL/REAL/SND, o porte dessas
bibliotecas serviria a eles também.

Atualização (seção 9): a versão de Xbox do mesmo jogo tem a física inteira em C++ comum, com o mesmo leiaute de
dados; ela vira o oráculo do algoritmo para o porte do microcódigo e do código VU0 em macro, o que reduz o risco 3.

## 9. A versão de Xbox

A cópia da versão americana de Xbox (NTSC-U) serve **só como referência de leitura**: o port continua usando os dados do
PS2 americano e o comportamento do PS2 como a verdade. A imagem e o que sai dela ficam fora do repositório (`xbox/` e
`out/`, no `.gitignore`).

### O que é a imagem

`007 - Nightfire (USA).xiso.iso` (XDVDFS; `python tools/xbox.py info|list|extract`). Dois executáveis, título
"007™: NightFire™", id 0x45410026, região América do Norte, versão 1, XDK 4831:

| | `Driving.xbe` | `default.xbe` |
|---|---|---|
| o que é | as missões de carro (o `DRIVING.ELF` do PS2) | o jogo a pé (o `ACTION.ELF`) |
| compilação | 18/10/2002 23:08:47 UTC (o mesmo dia do `DRIVING.ELF` do disco) | 20/10/2002 03:27:10 UTC |
| caminho do build | `D:\ToBurn\BondXbox\Final\BondXBOX.exe` (o PS2: `d:\ToBurn\BondPS2\PS2_EE_Release`) | `q:\SS\source\Xbox\BuildFinal\Bond.exe` |
| `.text` | 1.360.752 bytes (o MIPS do PS2: 2.217.724) | 980.076 bytes |
| seções do XDK | `D3DX`, `D3D`, `XGRPH`, `DSOUND`, `XPP`, `DOLBY` (o programa do DSP de áudio) | as mesmas e `XMV` (os filmes) |
| importações do kernel | 100 (`xboxkrnl` por ordinal) | 96 |

Nenhum dos dois tem símbolos, caminhos de fonte ou RTTI do jogo (só 7 classes da CRT; o jogo foi compilado sem RTTI);
o `Driving.xbe` tem 219 cadeias "Classe::membro" (enums do EAGL, `DynamicLoader::GetAddr/Resolve`). Os dados de carro
estão em `driving\misc.viv` e `mis01.viv` (o mesmo BIGF; `python tools/driving_data.py --root xbox/disco list misc.viv`),
com os render methods do EAGL de Xbox (`data\render\eaglrm.o`, `bondrm.o`: objetos ELF como os do PS2, mas com
`__EAGLMicroCodeVS`/`__EAGLMicroCodePS`, os programas de vértice e de pixel do NV2A).

### No Ghidra

O Ghidra não lê XBE sem um carregador de fora (não baixamos o da comunidade). `python tools/xbox.py pe <xbe> <exe>`
escreve um **PE32 equivalente**: base 0x10000, cada seção do XBE no mesmo endereço virtual (RVA = va - base) com os seus
dados brutos, a posição no arquivo igual ao RVA (um PE "plano", porque os va do XBE não são múltiplos de página:
SectionAlignment = FileAlignment = 0x20), a entrada decodificada (a chave de varejo), sem importações, relocações nem
diretórios, subsistema 14 (Xbox). As características vêm das flags do XBE (gravável, executável), mas o `.rdata`, o
`.data`, o `DOLBY` e as imagens `$$XT/XSIMAGE` ficam como dados: o imagebld marca quase tudo como executável. O que se
perde: o cabeçalho do XBE (o console o mapeia em 0x10000; ali ficam os cabeçalhos do PE) e os nomes de seção com mais de
8 letras (`$$XTIMAGE` vira `$$XTIMAG`). O comando também escreve `<exe>.labels.tsv` para o `ApplySymbolTable.java`: a
tabela de thunks do kernel (o endereço vem do cabeçalho, com a chave; cada entrada `xboxkrnl_<ordinal>`), o início de
cada seção do XDK com a biblioteca e a versão, a entrada e o diretório de TLS. `python tools/xbox.py test` monta um XBE de
cinco seções fora do alinhamento de página e lê de volta os cabeçalhos do PE escrito.

```
NF_DATA=<checkout com os dados> tools/ghidra_xbox.sh pe        # out/xbox/Driving.exe, default.exe e os .labels.tsv
NF_DATA=... tools/ghidra_xbox.sh import                        # out/ghidra_xbox, projeto "Xbox" (x86, "windows"; 1 minuto)
NF_DATA=... tools/ghidra_xbox.sh features                      # out/xbox/Driving_features.tsv
NF_DATA=... tools/ghidra_driving.sh features                   # out/driving/features.tsv (o PS2, com os nomes do SYM)
python tools/xbox_match.py match --evaluate                    # out/xbox/match.tsv, xbox_names.tsv e as estatísticas
NF_DATA=... tools/ghidra_xbox.sh names out/xbox/xbox_names.tsv # os nomes do PS2 no projeto do Xbox
NF_DATA=... tools/ghidra_xbox.sh decomp "re:RigidBody::.*"     # out/xbox/decomp/
NF_DATA=... tools/ghidra_xbox.sh callers DAT_00234e30          # quem lê ou grava uma global
```

A análise automática do Ghidra (com o FID do MSVC, que reconhece parte da biblioteca C++) acha ~6.400 funções; o
`XboxVtables.java` (pós-script do `import`) acha as tabelas virtuais, que no MSVC sem RTTI são só ponteiros encostados no
`.rdata`: corta as sequências onde o código grava o endereço (o construtor), dá o rótulo `vtable_<endereço>` a 425 delas
(o PS2 tem 425 tabelas virtuais no SYM) e cria as 639 funções que só as tabelas apontavam: 7.016 no total (sem os
thunks). Delas, ~4.760 ficam abaixo de 0x112000, o código do jogo; daí em diante vêm a matemática, o EAGL, o REAL e
a CRT de Xbox.

### O casamento (`tools/xbox_match.py`)

As duas compilações saem do mesmo código-fonte, mas de compiladores e processadores diferentes (GCC 2.95 MIPS, MSVC 7
x86): nenhum byte de código em comum. O `ExportMatchFeatures.java` exporta dos dois projetos, por função, o que sobrevive
à troca: as cadeias lidas, as constantes float (no PS2 também as montadas em registrador, `lui` [+ `ori`] e `mtc1`; no
x86 as lidas da memória como float ou double e os imediatos com cara de float), as inteiras raras, as variáveis globais
lidas ou gravadas, as chamadas na ordem do código e as tabelas de ponteiros. O casador:

1. **Características**: cada uma pesa log(N / df) do lado onde é mais comum (as que só um lado tem não contam); par
   mútuo com margem. Cadeias raras (no máximo três funções de cada lado) dão confiança 3; duas ou mais constantes raras,
   confiança 2. 357 pares.
2. **Vizinhos**: os chamados e os que chamam de cada par casado votam nos vizinhos sem par do outro lado; aceito com
   dois votos (ou um e uma característica rara), margem, e **concordância**: a maior parte dos vizinhos já casados das
   duas funções tem de corresponder (sem isso, as funções utilitárias que muitos chamam juntavam votos por acaso).
3. **Tabelas virtuais**: no PS2 cada tabela começa na `__tf`; uma tabela do Xbox com as mesmas entradas casadas na
   mesma posição (sem empate entre tabelas: métodos herdados aparecem em várias) dá as outras entradas.
4. **Globais**: variáveis casadas por voto dos pares que as usam viram características (o ajuste da física é um bloco de
   globais com os mesmos valores iniciais dos dois lados).
5. **Ordem das chamadas** (confiança 1, sugestão): entre duas chamadas casadas, os chamados sem par são alinhados pelo
   tamanho (razão x86/MIPS de 0,92 nas instruções, desvio 0,26 no log), recusando os pares cujos vizinhos discordam.
6. **Leitura**: seis pares conferidos à mão que os passos acima não acham (tabela `MANUAL`), e 23 funções de vetor e
   matriz só do Xbox com nomes nossos no namespace `XboxMath` (`Vec3Cross`, `Vec3ScaleAdd`, `Vec3Normalize`,
   `QuatToMatrix`, `Vec3TransformVector`...): no PS2 elas são VU0 em linha, sem nome no SYM.

Resultado (2.332 pares; 7.016 funções do Xbox, 8.694 do PS2):

| Método | Confiança | Pares |
|---|---|---:|
| cadeias | 3 | 329 |
| leitura | 3 | 6 |
| vizinhos | 2 | 1.408 |
| constantes | 2 | 46 |
| tabela virtual | 2 | 34 |
| ordem das chamadas | 1 | 509 |

| Funções do PS2 com nome de confiança 2-3 do SYM | Funções | Com par de confiança >= 2 | % das funções | % dos bytes |
|---|---:|---:|---:|---:|
| **física e veículos** (`P*`, `RigidBody`, `Newton`, `OBB`) | 212 | 69 | 33% | **88%** |
| IA (`AI*`) | 322 | 124 | 39% | 75% |
| simulação, missões e armas | 238 | 82 | 34% | 73% |
| HUD, galeria, carga (`G*`) | 218 | 93 | 43% | 72% |
| mundo (`W*`) | 211 | 81 | 38% | 62% |
| áudio e filmes do jogo | 386 | 125 | 32% | 59% |
| render e câmeras (`R*`) | 662 | 203 | 31% | 57% |
| personagens (`Act*`) | 181 | 49 | 27% | 51% |
| eventos de missão (`E*`) | 743 | 67 | 9% | 41% |
| **código do jogo** (sem as bibliotecas) | 3.809 | 994 | 26% | **57%** |
| bibliotecas: EAGL, REAL, SDK da Sony, GCC, STL | 1.300 | 66 | 5% | |

As funções que ficam sem par são sobretudo as pequenas: o MSVC expande em linha os acessores e os destrutores que o GCC
chama, então o que casa são as funções grandes (daí a diferença entre a porcentagem das funções e a dos bytes). As
bibliotecas quase não casam porque são outras no Xbox (EAGL e REAL de Xbox, a CRT da Microsoft, D3D no lugar do SDK da
Sony). Do lado do Xbox, ~1.470 das ~4.760 funções do jogo ficam com par de confiança >= 2. Das 447 funções do PS2 com
VU0 em macro (7.282 instruções), 256 têm par de confiança >= 2 (61% das instruções VU0); das 42 com 40 ou mais, 29.

**Confiança.** `--evaluate` esconde as cadeias e as constantes de metade dos pares por cadeias (167) e refaz tudo: 136
voltam por outro caminho, 132 ao mesmo par (97%; pelos vizinhos 119 de 121, 98%; pela ordem das chamadas 13 de 15, 87%),
e os outros pares saem iguais (2.144 de 2.147). A amostra puxa para funções com cadeias, maiores que a média; os pares de
confiança 1 são sugestão. Conferidos lendo o código dos dois lados: os da física abaixo,
`ESimFrameUpdate::~ESimFrameUpdate`, `Simulation::SimulateGame`, as cortinas de visibilidade.

### A física, lado a lado

O PS2 roda a física em três programas da VU0 no `.vutext` (`python tools/vudis.py DRIVING.ELF --prog=N`): 0 o
`VU0_charAnim` (200 instruções), 1 o `VU0_physics` (351: `VU0MICRO_MultiplyMatrix` em 0x22, `ResolveLeverForces` em
0x34, `UpdatePositionAndOrientation` em 0xad, em instruções; o SYM dá os rótulos em bytes) e 2 o `VU0_culling` (189:
`CheckAgainstVisCurtains` em 0x92, `CheckPositionAgainstVisCurtains` em 0xa8). `Simulation::SimulateGame` sobe o
microcódigo e passa os corpos em etapas: colisão entre objetos, com o chão (`CollideWithGround` → `ResolveLeverForces`),
amortecimento angular, o passo (`UpdatePositionAndOrientation(1.0)` → `Timestep_LoadAndStartVU0`), as forças iniciais,
os corpos simples. O passo da simulação é fixo, 1/60 s, nos dois (`ESimFrameUpdate::~ESimFrameUpdate` soma 0.016666668
ao relógio a cada quadro da simulação; o `SimulateGame` do Xbox tem as mesmas etapas, na mesma ordem).

1. **O passo do corpo rígido** (microcódigo `VU0MICRO_UpdatePositionAndOrientation`). No PS2,
   `Timestep_LoadAndStartVU0` (0x1f89e0) carrega uns 16 vetores nos registradores da VU0 (`lqc2`, `ctc2`, `vsqi`), com a
   constante (dt, 0.5, 1000) montada uma vez, e dispara o microcódigo com `vcallms 0x568`; o resultado do corpo anterior
   é recolhido no começo da chamada seguinte (`RigidBody::Timestep_CollectVU0data`), em fila. O decompilador do Ghidra
   não mostra os registradores de destino, então a fórmula só aparece lendo o microcódigo. No Xbox, uma única função em
   C++ (0xb0ec0, sem nome no SYM: as três partes do PS2 viraram uma) diz tudo em ordem: limita o momento linear a ±100 ×
   massa (a massa vive no w do momento, +0x4c); momento += força × dt e momento angular += torque × dt; v = P / m;
   posição += v × dt; ω = I⁻¹(mundo) · L, limitada a ±10 por componente, e L recalculado a partir dela; dq = 0,5 ×
   (ω dt) ⊗ q; q += dq; normaliza q; refaz a matriz de rotação e I⁻¹(mundo) = R · diag · Rᵀ; transforma os pontos de
   alavanca e as 8 quinas da caixa (buffer duplo, +0x4dd); `ControlSleep`; `UpdateZonalInfo`. O microcódigo faz o mesmo
   na mesma ordem (`MAXx/MINIy` contra os limites, `DIV Q` para 1/m, `OPMULA/OPMSUB` no produto do quatérnio, `MULy` por
   0,5, `RSQRT` na normalização, a matriz do quatérnio, os três `MULA/MADDA/MADD` do tensor, o laço de 16 pontos).
2. **As forças de alavanca** (`VU0MICRO_ResolveLeverForces`). No PS2, `RigidBody::ResolveLeverForces` (0x1f91e8) monta um
   bloco de parâmetros (o atrito e a restituição escolhidos pelo estado do dono), envia as alavancas e dispara `vcallms
   0x1a0`; `Resolve_CollectVU0data` recolhe. No Xbox (0xae150) é o laço em C++: para cada alavanca, a velocidade do
   ponto (v + ω × r), a velocidade normal limitada, o impulso -(1 + e·|vn|)·vn / (1/m + n · ((I⁻¹(r × n)) × r)), o atrito
   na tangente normalizada, e só se o impulso empurra para cima: P += J, L += r × J, v e ω atualizados antes da alavanca
   seguinte (impulsos sequenciais). O microcódigo tem os mesmos passos (`ADDq` do 1/m, `ABS`/`MINIw` no limite, `DIV`,
   `RSQRT` da tangente, o teste do sinal por `FMAND`, o laço com v e ω refeitos dentro).
3. **`RigidBody::GenerateImpulse`** (0x1fb318; 47 instruções VU0 em macro, `vopmula`/`vopmsub`, `vmulabc`, `qmfc2`) e
   `ResolveCollision`: no Xbox (0xafdb0, 0xaec10) o mesmo código com as funções `XboxMath`; os mesmos campos do
   `COLLISION_INFO` (+0x40 a +0x4e), as mesmas constantes (0,707, 0,2, 0,001, o limite do impulso) e os mesmos desvios
   pelo dono (as chamadas virtuais +0x1d4/+0x9c/+0x1dc do PS2 são +0xe4/+0x48/+0xe8 no Xbox: a tabela virtual do MSVC
   não tem as entradas da `__tf` nem o delta).
4. **`ApplyAngularDamping`** (0x1f8088 / 0xad130) e **`ControlSleep`** (0x1fcfc0 / 0xb09d0) são linha a linha iguais.
5. **As cortinas de visibilidade** (`VU0_culling`): `AddCurtain` (0x22f4a0) e o teste de uma esfera contra as cortinas
   (0x22f5a8, `vcallms 0x92`) são, no Xbox, 0xd0d60 (guarda até 63 cortinas de 0x40 bytes, transpostas) e 0xd0f50 (cada
   cortina dá quatro distâncias de plano; a esfera fica invisível se as três primeiras e a quarta passam do raio).

O **leiaute dos dados** é o mesmo. O `RigidBody`: quatérnio +0x00, posição +0x10, velocidade +0x20, velocidade angular
+0x30 (a diagonal do tensor inverso nos w dessas três), momento +0x40 (a massa no w, +0x4c), momento angular +0x50, o
ponteiro dos dados do corpo +0x5c, força +0x60 (o w são os bytes de estado +0x6c..+0x6f), torque +0x70. O
`COLLISION_INFO` (+0x40..+0x4e) e os dados do corpo (a orientação e o tensor inverso no mundo em +0x00 e +0x40, as
meias-medidas da caixa em +0x4c0, o número de pontos em +0x4dc, os indicadores em +0x4fc..+0x4ff) também. As
**constantes** de ajuste são globais com os mesmos valores iniciais (amortecimento 0,995, limiar do sono 5, limite
angular 10, limite linear 100 × massa, 0,18, 0,25, 0,7).

As **diferenças**:
- o `Simulation` do Xbox tem 64 vagas de corpo rígido (o PS2, 48; os 96 corpos simples são iguais), então os
  deslocamentos dentro do `Simulation` mudam (0x270 → 0x2b0, 0xdb4 → 0xe34), e a busca do corpo pelo índice
  (`FUN_001ff088` no PS2) foi expandida em linha;
- o PS2 trabalha em fila (dispara a VU0 para um corpo e recolhe o anterior); o Xbox, um por vez. O resultado é o mesmo;
- a **aritmética**: o Xbox usa o x87 (resultados intermediários em precisão estendida) e o SSE (`rsqrtss`, 1/√x
  aproximado em 12 bits, nas normalizações de `XboxMath`), o PS2 a VU0 (precisão simples sem denormais nem infinitos,
  `RSQRT`/`DIV` próprios) e a FPU do EE. Os dois não dão os mesmos bits: o Xbox é um oráculo do **algoritmo** (a fórmula,
  a ordem das operações, os campos e as constantes), não um oráculo **numérico**; a ordem exata das operações e as
  máscaras de destino continuam vindo do microcódigo e da desmontagem do PS2.

Veredito: para a física, sim. As funções que no PS2 são microcódigo ou VU0 em macro (o pior caso da decompilação: o
Ghidra perde os registradores e as máscaras) são, no Xbox, C++ legível que diz o algoritmo, com o mesmo leiaute e as
mesmas constantes. Os trechos decompilados dos dois lados ficam em `out/xbox/compare/` (`ps2/`, `xbox/`, e os três
programas da VU0 em `vu0_prog*.txt`).

### O render, por amostra

Dois métodos que existem nos dois conjuntos, `LitGouraud` e `LitTextureEnvmap_Skin`:

- No Xbox o programa de vértice já vem montado pelo XGAssembleShader (`<método>__EAGLMicroCodeVS`: cabeçalho 0x2078 e
  instruções de 128 bits do NV2A). `python tools/xbox.py vsh eaglrm.o LitGouraud1` desmonta (os campos são os que os
  emuladores documentam; o resultado se confere pelo fim padrão que o montador acrescenta, a escala e o deslocamento da
  janela de vista em c[-38]/c[-37]). O `LitGouraud` tem 21 instruções: posição pela matriz c[-96..-93], normal pela 3x3
  c[-92..-90] e normalizada, três luzes direcionais (N · L de cada uma pela matriz das direções, `MAX` com 0, soma das
  cores), `oD0` = soma × cor do vértice + cor do vértice × ambiente. O `LitTextureEnvmap_Skin` (31) tem um osso por
  vértice (índice em v3), as mesmas três luzes, e o mapa de ambiente esférico `oT1` = 0,5 + 0,5 × (N · linha 0, -N ·
  linha 1) da vista (o fator vem de uma constante).
- No PS2 o microprograma da VU1 (`python tools/vudis.py --eagl eaglrm.o LitGouraud`: o símbolo aponta o número de
  instruções, e o MPG com a tag REF de DMA que o envia fica logo antes) faz a mesma luz: normaliza as três direções
  (`ERLENG`), passa-as para o espaço do objeto, N · L, `MAX` 0, a matriz das cores mais o ambiente, vezes a cor do vértice,
  limitado a 255; o `LitTextureEnvmap_Skin` usa 0,49 (no código) no mapa esférico. O resto do programa (195 e 734
  instruções) é o que o NV2A faz em hardware: perspectiva, recorte (`CLIP`, o bit ADC), ponto fixo para o GS, montagem do
  pacote GIF, pele com vários ossos.

Veredito: os programas de vértice do Xbox são legíveis e dizem a fórmula de luz de um método em poucas linhas, e ela
bate com o programa da VU1 do mesmo nome; mas só 43 nomes são comuns aos dois conjuntos (128 no Xbox, 108 no PS2,
contados pelos símbolos `__EAGLMicroCode*`; a primeira versão deste texto dizia ~17), os formatos de vértice são
outros e a parte difícil do porte do render (ler os pacotes de DMA dos modelos, o recorte, os efeitos do GS) não tem
equivalente no Xbox. Serve de referência conceitual e para conferir uma fórmula; não muda o risco 2.

### O `default.xbe` e o ACTION.ELF

O jogo a pé do Xbox é o mesmo código da Eurocom compilado pelo MSVC: 799 das 2.738 cadeias do `default.xbe` também estão
no `ACTION.ELF`, e até as etiquetas de memória continuam `malloc_PS2_*`. Para o porte do `ACTION.ELF` ele pouco acrescenta:
o `ACTION.ELF` tem a sua `.symtab` com todos os nomes, e 79% dos bytes já estão portados e testados; poderia servir, como
aqui, de oráculo do algoritmo para alguma das funções com VU0 em linha que faltam ou para conferir uma dúvida de leitura
(com o mesmo cuidado: o Xbox não dá os bits do PS2), e nada além disso. `tools/ghidra_xbox.sh import default` o importa
no mesmo projeto (não foi analisado aqui).

### Efeito no plano

- **Risco 3 (a física na VU0)**: cai de "alto" para "médio". O algoritmo de cada função de física, inclusive as que são
  microcódigo, está escrito em C++ no Xbox, com o mesmo leiaute; o trabalho passa a ser portar o C++ do Xbox conferindo
  contra o microcódigo e a desmontagem do PS2 a ordem das operações (para a fidelidade numérica), em vez de reconstruir o
  algoritmo a partir de registradores sem destino. O mesmo vale para as 256 funções com VU0 em macro que casaram (IA,
  câmeras, armas, veículos).
- **Risco 1 (nomes e decompilação lenta)**: um pouco menor nas funções grandes do jogo (57% dos bytes têm par), que no
  Xbox não têm as tabelas virtuais com delta nem os 128 bits do EE.
- **Risco 2 (o render)**: igual.
- **Estimativa**: fase 1 de 6-10 para 5-8 dias, fase 2 de 8-12 para 7-11, fase 3 de 10-15 para 9-14; o total de 30-45
  para **27-41 dias**, ainda duas a três vezes o porte feito do `ACTION.ELF` (a incerteza continua alta: o render e os
  formatos não mudam).
- **Como usar no porte**: para cada função do PS2, `out/xbox/match.tsv` (ou o nome no projeto `out/ghidra_xbox`) dá o par
  no Xbox; ler o Xbox para entender, portar do PS2 (os deslocamentos, as constantes, a ordem e os quirks são do PS2, como
  manda o CLAUDE.md), e usar o Xbox como segunda leitura nos testes (propriedades: conservação do momento num impulso,
  o quatérnio normalizado, os limites). Pares de confiança 1 e os do grupo das bibliotecas pedem conferência. Nunca
  copiar números do Xbox onde o PS2 tem outro valor (as 48 vagas, o 0,49 do mapa esférico).

## 10. O protótipo: o desenho

A primeira parte do porte (a fase 0 e o render da fase 1 da seção 6): um carro do EAGL desenhado no PC pelo caminho do
jogo, do arquivo da missão até o microprograma da VU1. O código fica em `src/driving/` (biblioteca `nf_drv`, namespace
`drv`, sem GPU) e `src/driving_app/` (o executável de desenvolvimento `nf_driving`, Direct3D 11). A física é outra frente
(`src/driving/physics/`, `nf_driving_check`).

### O que foi portado

| Peça | Arquivo | Origem |
|---|---|---|
| BIGF e RefPack | `bigf.h` | só do port (o jogo usa o REAL: `unpackz`/`REF_decode`); o diretório e a descompressão da seção 5 |
| SHPS para RGBA | `shps.h` | só do port: blocos de 16 bytes, 8 e 4 bits com paleta (CSM1, a troca de linhas da paleta), 16/32 bits, o bit 0x2000 (pixels embaralhados do PSMT8), mipmaps; o alfa fica na escala do GS (0x80 = 1,0) |
| CARP | `carp.h` | o UGroup (`UGroup::GetArray`, `GroupLocateTag`, `DataLocateTag`), as instâncias (`CARP::Instance`) e as listas de desenho por vista (`RSceneObj::GetViewDrawList`) |
| Objetos ELF do EAGL | `eagl_loader.h` | o `EAGL::DynamicLoader` (`Initialize`, `Resolve`, `FindSymbol`, `GetSymbol`, `GetAddr`, `RunConstructors`, `RunDestructors`, `Release`): os símbolos `__Tipo:::nome` reescritos, a tabela de hash de 256, a ordem de resolução (outros objetos, o resolvedor do dono, as variáveis registradas, `RUNTIME_ALLOC::`), as relocações R_MIPS_32/26/HI16/LO16, o ELF partido do CARP (`ELFd`/`ELFr`) |
| Modelo e primitivas | `eagl_model.h` | `EAGL::Model::Model` (os grupos visíveis, a lista de desenho), `EAGL::GeoPrim::Construct` (encadeia as primitivas de um grupo) e `EAGL::GeoPrim::Draw` (as operações 1, 3-7, 0xe/0xf, 0x10-0x13 da lista de comandos: o pacote de partida do microprograma, DMA pronto, palavras do VIF com cópia, o pacote do TAR, o GeoPrimState com FRAME/ZBUF/FBA, etiquetas CNT, saltos, a repetição por REF) |
| Estado do GS e texturas | `eagl_state.h` | `EAGL::GeoPrimState` (o pacote de 8 qwords: PRIM, TEST, ALPHA, FRAME, ZBUF, FBA), `RStateManager::GetGeoPrimState`/`ParseFirstStateTag` (as cadeias "alphatest=on;texalpha=deepA..."), `EAGL::TAR::TAR`/`BuildPacket` (TEXA, CLAMP, TEX1, TEX0, MIPTBP) |
| VIF1 | `vif.h` | só do port: a cadeia de DMA (CNT, REF, CALL, RET, END, TTE) e o VIF1 (UNPACK em todos os formatos, máscara, STROW/STCOL, STMOD, CYCLE, BASE/OFFSET com o buffer duplo, MSCAL/MSCNT/FLUSH, DIRECT) numa memória de VU de 16 KB |
| A família do carro | `mat_car.h` | o microprograma (abaixo) |
| A conferência | `mat_car_verify.h` | só do port: o microprograma original no interpretador da VU1 contra a referência (abaixo) |
| O carro | `car_model.h` | só do port: carrega os render methods, as texturas, o CARP e os construtores do jogo, executa as primitivas e decodifica cada lote |

As funções do jogo levam `// [driving] 0x<endereço> <Nome>` (o endereço do `DRIVING.ELF` do disco). As decompilações e
desmontagens usadas ficam em `out/decomp_driving/`, `out/decomp_driving_render/` e `out/disasm_driving_render/` do
checkout principal; os microprogramas em `out/driving/vu_<método>.txt` (`python tools/vudis.py --eagl bondrm.o <método>`).

### Como um carro chega à VU1

`CarModel::load` (o que o `RCARPFile` e o `RVehicle` fazem, sem o laço do jogo):

1. abre o `MISC.VIV` e liga `data\render\eaglrm.o` e `bondrm.o` (os 115 render methods; tipos registrados: `Model`,
   `EAGL::TAR`, `EAGL::GeoPrimState`), e lê as texturas do carro (`data\car\model\<carro>.ssh`), do `common.ssh`
   (`carS`, o mapa especular) e do `ext.ssh` (`bref`);
2. abre o `.crp` da missão: o CARP é um UGroup (`carp.h`); o modelo do EAGL são os dados `ELFd`/`ELFr` da raiz, ligados
   pelo `DynamicLoader` com o resolvedor do jogo (`GAME::`, `STATE::`, as matrizes `EAGL::ViewPort::gp*`, os
   `CarLighting*Props`, `ReflectionInfo1/2`, `DamageZones`, `SwitchData`, `CarShadowColour`);
3. para cada `__Model` (as 43 peças do Vanquish, `vanquish::{as  0000}::Model[0].00`...), cada grupo visível e cada
   primitiva: `GeoPrim_Draw` monta a cadeia de DMA, o VIF1 a executa e cada `MSCNT` é um lote (até 44 vértices) que o
   microprograma receberia, com o método pela fila de pacotes de partida (o endereço da VU é reaproveitado no meio da
   cadeia);
4. as instâncias do grupo `Arti` (`in#0000`, 64 bytes cada: rotação nas linhas 0-2, translação na linha 3; a peça pelo
   registro `as`, o nome do modelo pela `sr`) põem cada peça no carro: 57 no Vanquish, a carroceria em y = 0,696 (a
   origem fica no chão), as rodas em (±0,748; 0,353; 1,409) e (±0,748; 0,353; -1,269), as peças de um lado repetidas
   espelhadas em x. A lista de desenho da vista (`vd#0001`, a de fora: 47 instâncias, sem o painel, os ponteiros e o
   retrovisor interno; `vd#0002` é a de dentro, sem a carroceria) escolhe o que aparece. Sem isso as rodas, o capô e o
   porta-malas ficavam na origem e o forro do teto do interior aparecia 5 cm acima do teto.

### O render method reproduzido: a família MatCar

Quatro dos 13 métodos do carro são um só microprograma (`mat_car.h`, com o passo a passo e os endereços):
`MatCarTexturePacked` e `MatCarChrome` são o mesmo código, byte a byte; `MatCarTextureDullPacked` é o mesmo com outro
leiaute e uma passada; `MatCarDash` é o mesmo sem o passo 1 (entradas em float) e sem a passada 3. Por vértice:

1. (só os "Packed") STQ = u16 × I1 − 32 (I1 = 0x3a7ffff7, um pouco menos que 1/1024), normal = u8 × I2 − 1 (I2 =
   0x3c008080, pouco menos que 2/255), vetor e normal de dano = u16 × I1 − 32; o w da normal de dano é a zona;
2. dano: z = `DamageZones[zona]`; posição += vetor × z.x, normal += normal de dano × z.x; cor = int(max(0, 127 × (1 −
   vetor.w × z.y))), alfa_f = max(0, 128 × (1 − vetor.w × z.z)) (a cor de partida é vf00 + 127 = (127, 127, 127, 128));
3. três luzes direcionais com as linhas da `WorldMatrix` normalizadas pelo `ERLENG`: d = max(0, A n), cor = min(255,
   cor × (C0 × difusa × d.x + C1 d.y + C2 d.z + ambiente × prop. ambiente));
4. quatro luzes pontuais sem a normal: att = max(0, 255 − K |P − p|²), cor = min(255, cor + Σ C att);
5. posição × MVP, Q = 1/w, a névoa F = 255 − escala × (clamp(w) − perto), STQ × Q, FTOI4; o ADC dos recomeços de faixa
   (as 12 entradas `SplitReg`); passada 1: o State, o TAR da textura e os vértices;
6. passada 2, o especular: (`WorldMatrix` × `SpecularMat`) × `ReflectionInfo2.z` aplicada à normal dá ST = 0,5 + xy, Q
   = 1, cor = int(prop. especular × alfa_f), com o `EnvMapState` (no Dash, o `SpecMapState`) e o `SpecularMap`;
7. passada 3, o reflexo: v = posição × `ReflectionInfo2.x` + normal, a = `WorldMatrix` × v × `ReflectionInfo2.y`, ST =
   (0,5 + a.x, 0,5 + a.z), cor = int(min(prop. reflexo × alfa_f, 100000 × max(a.y, 0))), com o `ReflectionMap2`.

A referência na CPU (`car_reference`) faz as contas na ordem do microcódigo (produto e soma separados, FTOI0 trunca); os
testes (`tests/test_driving_render.cpp`) conferem valores calculados à mão a partir da leitura do microcódigo (a cor
95 de 127 × 0,75, o ADC com o AND 0xbffe, o reflexo que some abaixo do horizonte, a zona de dano que escurece e move, a
luz pontual, o Dash com o z do STQ do arquivo). O vertex shader do `nf_driving` refaz os passos 2-7 (o passo 1 vai para
a carga: `car_convert`), e o pixel shader faz o que o GS faz: MODULATE `floor(Ct × Cv / 128)` (o TCC 0 dos TAR do carro:
o alfa vem do vértice), DECAL, o teste de alfa com o AFAIL (um segundo desenho só RGB), a névoa, a mistura do ALPHA_1
((A − B) × C + D nos fatores do Direct3D), Z GEQUAL como LESS_EQUAL, sem descarte de faces, cor e névoa sem perspectiva
(o GS interpola na tela) e o STQ da passada 1 com perspectiva (S/Q no GS).

Divergências documentadas: o descarte de triângulos do recorte (CLIPw/FCAND/FCOR) não é feito (o Direct3D recorta); a
janela de vista do GS (VU 0 e 1) dá lugar à projeção do PC; e o que o jogo calcula em código que ainda não foi lido
vem do port: a `SpecularMat` é a rotação da câmera (o `RReflection` não foi lido), o `ReflectionMap2` é o `bref` estático
do `ext.ssh` (o jogo desenha o reflexo da cidade numa textura de 256×256 a cada quadro), as luzes são as do `.tun` da pista
(`Light1`/`Light2`/`AmbientSky` do conjunto `{World}`; o `RLightManager` não foi lido), as propriedades de luz são as do
`CarRender\default.tun` como o `RReflection` (0x001c0930) as lê, e as zonas de dano ficam em zero (o carro inteiro).

### A conferência contra o microprograma original

`nf_driving --verify` (e `--check --verify`, todos os carros) roda, em cada lote da família, o próprio microprograma do
render method no interpretador da VU1 da seção 11 (`platform/vu_interp.h`, a aritmética do PS2), sobre a memória da
VU como o `MSCNT` a deixa (a partir do par 2: o par 0 tem o bit E, e o `MSCNT` continua de onde o programa anterior
parou), e compara os pacotes de vértices que ele chuta com `car_reference`, valor a valor (`mat_car_verify.h`). Nos 62
carros com os dados completos (o `bus_tokyo` do `MISC.VIV` fica fora: lê zeros), 7.018 lotes e 494.099 vértices
(somando as passadas): nenhuma falha do interpretador, todo pacote presente; o STQ a no máximo 7,6e-6 (a VU trunca o
"− 32" do passo 1 onde o PC arredonda; nenhum acima de 2^-16 e de 4 ulps), a cor igual menos 80 componentes a 1 (de ~2
milhões: o truncamento), X e Y a no máximo 1/16 de pixel, o Z (perto de 2^24 × 16) a 24 valores acima de 2^-21 do
valor, a névoa igual, e o ADC: 177.434 vértices com o ADC do recorte só no PS2 (a divergência conhecida: com as matrizes
identidade da carga quase tudo fica fora do volume de recorte) e nenhum ADC que só a referência ponha. No Vanquish: 360
lotes, 24.739 vértices, STQ a 1,2e-7, cor, X, Y, Z e névoa iguais (dentro das tolerâncias).

### O que fica no substituto

Os métodos do carro fora da família vão para um substituto marcado nos relatórios (`substitutos`): os fluxos pela ordem
dos parâmetros do método, a textura e a primitiva do primeiro pacote, a luz e a névoa da família, só a passada base. No
Vanquish eram 29 de 389 lotes: `MatCarTextureSwitchPacked` (as lanternas que acendem pelos `SwitchData`: a família com um
passo a mais) e `MatCarGlassPacked` (o vidro), os dois agora reproduzidos (seção 14), `MatCarGouraudDullPacked` e
`BondCarShadow` (a sombra, que nenhuma instância usa: o jogo a desenha à parte).

### O censo dos 115 métodos

`nf_driving --census` agrupa os métodos pelo microprograma (o mesmo código, byte a byte) e por família (pelos parâmetros
e pelo código: pele, recorte, EFU, as passadas):

| Família | Métodos | Microprogramas distintos |
|---|---:|---:|
| 2D, linhas, depuração | 12 | 11 |
| água | 5 | 5 |
| ambiente (envmap, irradiância, reflexo) | 12 | 9 |
| animado (rolagem e troca de textura) | 10 | 10 |
| carro | 13 | 12 |
| constantes (`MatBondPerViewPort`, `MatCarPerCar`...) | 5 | 3 |
| gouraud | 10 | 8 |
| iluminado | 6 | 6 |
| partículas | 2 | 2 |
| pele | 4 | 4 |
| pele + ambiente | 12 | 9 |
| sombra | 8 | 8 |
| texturizado | 16 | 12 |
| **total** | **115** | **97** |

Os carros de todas as missões usam só 8 métodos. `nf_driving --check` carrega os 63 `.crp` de carro dos 9 arquivos
(37 modelos distintos, vários repetidos em mais de uma missão): todos carregam, sem mensagem do carregador, erro do VIF nem
operação não portada, menos o `bus_tokyo` do `MISC.VIV` (sem os dados comuns de uma missão: 6 referências não resolvidas,
que viram zeros). São 7.872 lotes e 184.261 triângulos; a família reproduzida desenha 7.093 lotes (90,1%): `MatCarDash`
1.313, `MatCarTextureDullPacked` 3.101, `MatCarTexturePacked` 2.144, `MatCarChrome` 535; no substituto
`MatCarTextureSwitchPacked` 368, `MatCarGlassPacked` 267, `BondCarShadow` 111, `MatCarGouraudDullPacked` 33.

### As capturas

`nf_driving --data <raiz> --viv MIS01.VIV --model vanquish --yaw 0.6 --width 1920 --height 1080 --screenshot
out/shots/drv/vanquish.png` desenha fora da tela e grava o PNG e um relatório (`.txt`: métodos, peças com caixa,
instâncias, texturas, o estado do GS de cada passada de cada desenho, os substitutos). Julgamento: o Vanquish inteiro
(peças, rodas e porta-malas no lugar), a placa "KE02 EWW" legível (a textura e o STQ certos, sem espelho), as lanternas,
os pneus com o desenho da banda, a pintura prateada com o especular e o reflexo; o carro da polícia alpina (MIS4) com
"POLIZEI" no capô e nas portas, o giroflex azul. O que se vê de errado é o vidro (substituto, preto). Custo: 184
chamadas de desenho por quadro, 10 mil triângulos: 1,76 ms por quadro a 1920×1080 com MSAA 4x no Radeon integrado
(569 quadros/s) e 0,22 ms na RTX 5090 (`--bench-frames 600`).

### A verdade do PS2 (o plano)

A conferência acima prova a matemática dos vértices contra o código do jogo; o que ela não cobre é o que vem antes
(as matrizes, as luzes, a `SpecularMat` e o reflexo que o jogo calcula por quadro e o port ainda supõe) e o que vem
depois (o GS: a textura, a mistura, a névoa). Para conferir pixel a pixel, um dump do GS do PCSX2 (nada foi baixado; o
usuário captura):

1. no PCSX2 (1.7 ou 2.x, renderer de hardware ou software), entrar na missão Paris Prelude e parar com o Vanquish parado
   e visível; Ferramentas > "GS Dump" (Ctrl+Shift+F8 em versões recentes; um quadro) grava `<serial>_<data>.gs` (ou
   `.gs.xz`/`.gs.zst`) na pasta `snaps`;
2. o dump tem o estado do GS (VRAM e registradores) e os pacotes GIF do quadro: os vértices que a VU1 mandou (XYZF2 com o
   ADC, RGBAQ, ST), com o TEX0/ALPHA/TEST de cada passada;
3. a comparação que vale: para um lote do Vanquish, os vértices do dump contra `car_reference` com as constantes do
   quadro (as matrizes e as luzes, que no dump vêm nos pacotes `MatCarPerCar`/`MatBondPerViewPort` do mesmo quadro e
   pela RAM no `tools/pcsx2_pine.py` da seção 11); a imagem do PCSX2 contra a do `nf_driving` com a mesma câmera; e as
   texturas da VRAM do dump contra o SHPS decodificado. Um leitor `tools/gsdump.py` (o cabeçalho do PCSX2, os pacotes
   de transferência GIF, a listagem dos vértices por PRIM) fica para quando houver um dump: o formato muda entre versões
   do PCSX2 e precisa de um arquivo real para não ser escrito às cegas.

### O que falta para o desenho da fase 1

- Os outros métodos do carro: `MatCarTextureSwitchPacked` (pequeno: a família + `SwitchData`), `MatCarGlassPacked`,
  `MatCarGouraud*`, `MatCarGlass`, `MatCarTexture`/`MatCarTextureDull` (as variantes sem "Packed") e a sombra
  (`BondCarShadow`, com o `RVehicle` que a posiciona).
- O que o jogo calcula por quadro: o `RReflection` (a textura de reflexo de 256×256, a `SpecularMat`), o `RLightManager`
  (as luzes da pista e as pontuais), o `RVehicle` (os ossos do `Skel`: as rodas girando, a direção e a suspensão de
  `ComputeWheels`, o capô e o porta-malas abertos; os ponteiros do painel por `GetNeedlePos`; as zonas de dano da física),
  o recorte por triângulo, as palavras ainda não lidas das instâncias (a ordem/LOD na linha 3).
- O mundo: o CARP da pista (494 objetos `Arti` em Paris, o `Map`, o `CDat`, a rede de estradas `RNgp`), os métodos
  dele (`MatBondTexture*`, `ObjTexturePacked`, os de água e de céu), a neblina e a câmera de perseguição.

### Estimativa atualizada

A fase 0 está feita para o render (os leitores, o carregador, o modelo, o VIF, o censo; o `nf_driving_check` é da frente
da física) e um terço do render da fase 1 (um carro com a família que cobre 90% dos lotes dos carros). O que a seção 6
temia mais no render se confirmou administrável: os modelos são as listas de comandos do `GeoPrim::Draw` e o VIF faz o
resto, um microprograma se lê em algumas horas com o `vudis.py`, e o interpretador da seção 11 confere cada família
contra o código original nos dados reais. Sobram para a fase 1, no render, umas 2-4 famílias de métodos do mundo e os
quatro métodos do carro (3-5 dias), além da física e da câmera. O risco 2 (o render) cai de "alto" para "médio"; a
estimativa total fica nos 26-39 dias da seção 11.

## 11. O protótipo: a física

A metade da física do protótipo da fase 1 (seção 6): o corpo rígido do `DRIVING.ELF` portado, com o microcódigo da VU0
reescrito à mão em C++, e conferido bit a bit contra o código original do PS2 rodando em interpretadores (a VU0 e o EE).
O carro em si (o `PBondCar`, as rodas, o mundo da pista) ainda não: o que existe anda, cai, bate no chão, gira, para e
dorme como no PS2, para qualquer corpo.

### O que foi feito

| Peça | Onde | O que é |
|---|---|---|
| a aritmética do PS2 | `src/platform/ps2_float.h` | a FPU do EE e as VUs bit a bit: sem denormais (±0), sem infinitos nem NaN (expoente 255 é normal, o estouro satura em ±0x7fffffff), truncamento; soma e produto exatos em `double` e truncados; o quociente e a raiz em `double` truncados (nunca cruzam a fronteira de um float sem ser exatos); `RSQRT` em duas etapas (raiz truncada, depois a divisão) |
| o interpretador das VUs | `src/platform/vu_interp.h` | VU0 (4 KB + 4 KB) e VU1 (16 KB + 16 KB), modo micro (`run`) e modo macro (`macro`, as instruções COP2 que o EE manda); todas as instruções de cima e de baixo, campos de destino e difusão, ACC, I (bit I), Q e P com as latências (DIV/SQRT 7, RSQRT 13, a EFU 11 a 54; ler antes dá o valor velho, WAITQ/WAITP e uma segunda operação esperam), o pipeline do FMAC (4 ciclos) nos indicadores MAC/estado/recorte, a espera do hardware nos VF lidos cedo (só conta ciclos: o valor é o novo), o delay slot dos desvios e do bit E, desvio no delay slot de outro, o "VI backup" do PCSX2 (um desvio logo depois de uma conta inteira no mesmo VI lê o valor velho), na mesma instrução a de baixo lê os registradores de antes da de cima e é descartada se as duas escrevem o mesmo VF, XGKICK na VU1 guardando o pacote do GIF até a tag de EOP, XTOP/XITOP, R (RINIT/RNEXT...) |
| as operações da VU em C++ | `src/platform/vu0_macro.h` | `add`, `mulabc`, `maddbc`, `opmula`/`opmsub`... com campo de destino, a mesma aritmética: o que o código portado usa no lugar da VU0 em macro e o que os portes à mão do microcódigo usam |
| o interpretador MIPS | `src/data/mips_interp.h` | agora com a VU0 opcional (`attach_vu0`: `lqc2`/`sqc2`, `qmfc2`/`qmtc2`/`cfc2`/`ctc2`, as instruções em modo macro, `vcallms`/`vcallmsr`), as MMI que entrelaçam quadwords (`PEXTLW`, `PEXTUW`, `PCPYLD`, `PCPYUD`, `POR`, `PNOR`), `SQRT.S`, `RSQRT.S`, `MAX.S`, `MIN.S`, `ADDI`, `SYNC`, `CACHE`; o resto como antes (o `nf_loader_check` continua com 36 fases e 0 diferenças, os construtores estáticos do `ACTION.ELF` iguais) |
| o corpo rígido | `src/driving/physics/rigid_body.h` | `RigidBody` (0x80 bytes) e `RigidBodyInfo` (0x500) com os leiautes do PS2, a área de rascunho do scratchpad, as constantes de ajuste (o bloco de `.data` 0x3390cc..0x339177 com os nomes do registro "Physics:Rigid"); `InitRigidBodySystem`, `ResetRigidBodySP`, o construtor e o destrutor, `InitLevers`, `SetOrientMat`, `GetBodyTensorMatrix4`, `StoreBodyMatrix`, `StoreInvWorldTensor`, `UpdateZonalInfo`, `ApplyAngularDamping`, `ControlSleep`, `ForceToSleep`, `ResolveMassScaledForce`, `ResolveTorque`, `SetAngularMomentum`, `ApplyInitialForcesAndTorques`, `GetOrientToGround`, `CollideWithGround`, `TempGetHeightInformation`, o construtor estático (as razões das constantes) |
| o microcódigo | `src/driving/physics/vu0_physics.h` | os quatro pontos de entrada do `VU0_physics` portados à mão (0x000 quatérnio em matriz, 0x022 `VU0MICRO_MultiplyMatrix`, 0x034 `VU0MICRO_ResolveLeverForces`, 0x0ad `VU0MICRO_UpdatePositionAndOrientation`), sobre o estado de uma `VuMachine` (os temporários e as pistas que sobram incluídos); `Timestep_LoadAndStartVU0`, `Timestep_CollectVU0data`, `UpdatePositionAndOrientation`, `ResolveLeverForces`, `Resolve_CollectVU0data`, com a fila do PS2 (o corpo em voo na VU0 é recolhido quando o próximo começa); um modo que roda o microcódigo original no interpretador (o mesmo estado tem de sair) |
| os choques | `src/driving/physics/collision.h` | `GenerateImpulse` (o impulso de um prédio: as constantes `BUILDING_*`), `ResolveCollision` (o choque entre dois corpos) com `ModifyLevers` e `ScaleObjObjForces`, `Util_Bound`, `Util_GenerateCarTensor`, `ConvertTo/FromLocalAngularMomentum`, `ConvertWorldToLocal/LocalToWorld`; no dono, `GetHitPoints` e `PlayAnimation` (não virtuais no PS2) |
| a simulação | `src/driving/physics/simulation.h` | a parte da `Simulation` que passa os corpos: as 48 vagas, o passo (1/60 truncado, 0x3c888888), `GetRigidBody`, `GetRigidBodyInfo`, `CopyRigidBodiesTo/FromScratchPad`, `AdvanceStep`, `SimulateGame`, a parte da física de `ESimFrameUpdate` |
| o chão | `src/driving/physics/world.h` | o `WWorldPos` (0x40 bytes) e a interface `WorldCollision` (as três chamadas que a física faz ao mundo: `FindClosestFace`, `MakeFaceAtPoint`, `SurfaceBumpHeight`); `FlatGround`, um plano de teste |
| a REAL | `src/driving/real/vu0_math.h` | `VU0_quattom4`, `VU0_m4toquat`, `VU0_MATRIX3x4_mult`, `VU0_MATRIX4_mult` |

O que a física faz, lido no microcódigo e no EE (o Xbox explicou o algoritmo; os números e a ordem são do PS2):

- **O passo** (`UpdatePositionAndOrientation(1.0)`, 0x0ad): Euler simplético. O momento é limitado a ±`M_LIMIT` × massa
  (100) em cada eixo *antes* de somar a força × dt; o momento angular soma o torque × dt; v = P / m; ω = I⁻¹(mundo) L,
  limitada a ±`AM_LIMIT` (10 rad/s) em cada eixo, e L é refeito dessa ω (R I Rᵀ ω, com a inércia do corpo); posição +=
  v dt; o quatérnio vai a 1000 q antes de q' = 1000 q + ½ (ω dt ⊗ 1000 q), depois é normalizado com `RSQRT` (a escala
  muda os bits do resultado); a matriz de q', I⁻¹(mundo) = Rᵀ diag(I⁻¹) R, e as 16 alavancas giradas para o mundo
  (sempre as 16, usadas ou não). O dt entra numa constante (dt, 0,5, 1000, 0) montada na primeira chamada e nunca
  mais (`0x339174`).
- **O chão** (`CollideWithGround` → `ResolveLeverForces`, 0x034): cada alavanca abaixo do chão (até 12; nos veículos
  só as de 4/8 a 12, conforme a velocidade e a inclinação) dá um impulso, em sequência (a velocidade de uma já leva o
  impulso das anteriores): j = -(1 + e) vn / (1/m + n · ((I⁻¹ (r × n)) × r)), com e = min(|vn|, `MICRO_THRESHOLD`) ×
  `GROUND_RESTITUTION` / `MICRO_THRESHOLD` (a razão é calculada no construtor estático, com os valores do `.data`,
  antes do `.tun`); o atrito é a direção da velocidade tangencial vezes (n · F/m) × (massa / 8 × 0,05); e o impulso só
  é aplicado se o y dele for positivo (o FMAND do sinal de y: uma normal "para baixo" nunca puxa). Depois o corpo sobe
  pela normal da alavanca mais funda vezes a penetração. A normal do chão tem o y limitado a 0,9999 (0x3f7ff972): um
  chão plano fica levemente inclinado para o lado do x e z que a normal tinha.
- **As forças do quadro** (`ApplyInitialForcesAndTorques`, no fim do quadro, para o seguinte): força e torque zerados,
  a gravidade m × `GRAVITY` (-34 m/s²; na água × 0,1 e um arrasto de -0,1 v), e um objeto comum parado com três
  alavancas no chão perde 4 × `GROUND_FRICTION_COEFF` (20%) da velocidade por quadro. O primeiro quadro de um corpo
  novo não tem gravidade (o construtor zera a força).
- **O sono** (`ControlSleep`, na coleta do passo): |v|₁ e |ω|₁ abaixo de `SLEEP_VEL` (5; um quarto disso nos veículos) e
  três alavancas no chão: tudo zera e o corpo para de ser simulado (estado 1). Um veículo de pé (o y do "para cima"
  acima de 0,7) nunca dorme.
- **O amortecimento** (`ApplyAngularDamping`): L × 0,995 por quadro, só nos objetos comuns (tipo ≥ 4) sem alavanca no
  chão.
- **O choque entre dois corpos** (`ResolveCollision`, 0x1f9d98, com `ModifyLevers` e `ScaleObjObjForces`; quem o chama,
  `CollideWithObject`, ainda não): não é a física de corpos rígidos. Corpos que já se afastam a mais de 10 m/s pela
  normal não reagem; o tamanho do impulso é max(`SPEED_SCALE` |Δv|, profundidade × `DEPTH_FORCE_SCALE`, 1), ao
  quadrado, limitado por `TOTAL_FORCE_LIMIT` × max(|vA|², |vB|²) / 1600 (entre 5 e 40); entre veículos cada um leva
  a massa do outro / soma (o momento não se conserva: o mais leve muda de velocidade na razão do quadrado das massas),
  sem y no momento, e o braço do choque vai para baixo numa batida de frente ou traseira (`ModifyLevers`, alinhados
  acima de 0,866); com objetos, uma "massa de choque" (a soma acima de 1500 kg cada, senão 0,75 da menor), o y do
  impulso de um objeto vira |y|, cada componente fica em ±10 × massa e acima de 15 m/s o objeto não leva nada; o
  veículo leva 0,1, sem y e sem giro; `ScaleObjObjForces` corta quem sairia mais rápido que 1,05 × a maior velocidade
  de antes (pela razão dos quadrados, não pela raiz: corta mais que o necessário) e reforça quem bateu num prédio. O
  dono entra pela vida (`GetHitPoints`), pela animação 12 (`PlayAnimation`) e pelo SMissionManager +0x350.

Quirks preservados: as razões das constantes calculadas antes do `.tun`; o momento angular inicial do construtor é
ω / I⁻¹ componente a componente com a ω do mundo (só certo com a orientação identidade); a linha 3 do tensor inverso
(`RigidBodyInfo` +0x70) e o w de alguns temporários são lixo da pilha no PS2 (zero aqui; nada os lê); o w das
alavancas no mundo e de `RigidBodyInfo` +0x4c carregam sobras de pista de vf11, vf12 e vf15 (iguais nos dois modos,
nada as lê); um dono nulo (o PS2 sempre tem um) não é chamado (conta em `physics_pending`).

### A conferência (`nf_driving_check`)

```
./build/Release/nf_driving_check.exe <raiz dos dados> [--casos N] [--quadros N] [--semente N]
```

1. **Cobertura**: os três programas da VU0 (200, 351 e 189 instruções) e os 115 render methods da VU1 (`eaglrm.o`, 53,
   22.614 instruções; `bondrm.o`, 62, 34.816), quando extraídos para `out/driving/files/MISC.VIV`: nenhuma instrução
   desconhecida para o interpretador.
2. **O microcódigo original no interpretador × o porte à mão**, do mesmo estado: com `--casos 8000`, 32.000 casos
   (os quatro pontos de entrada, 7/8 em faixas realistas: corpos de 1 a 3000 kg, momentos acima do limite, corpos
   parados (`RSQRT` de zero), normais para baixo, verticais exatas, 1 a 12 alavancas; 1/8 com todos os bits
   aleatórios, denormais e expoente 255 incluídos): **0 diferentes**, o estado inteiro da VU0 (32 VF, ACC, VI, Q e os
   4 KB de dados) igual bit a bit. Os indicadores não entram (o porte não os mantém; o EE não os lê depois).
3. **O passo inteiro dos dois jeitos** (`ESimFrameUpdate` num chão plano: um cubo caindo reto, uma prancha girando, uma
   caixa jogada, uma caixa do tamanho de um carro, uma caixa dormindo): 600 quadros com os corpos, os `RigidBodyInfo`
   e a VU0 iguais bit a bit em todos os quadros; os quatro corpos que caem param e dormem com o centro a 0,2 a 1,6 cm
   da meia-altura (0,5065 para 0,5; 0,2395 para 0,25; 0,2971 para 0,3; 0,5982 para 0,6).
4. **O código original do EE × o porte em C++**: o `DRIVING.ELF` carregado no interpretador MIPS com a VU0 ligada (o
   microcódigo original no interpretador das VUs), as estruturas copiadas para a RAM emulada, as funções chamadas
   pelos seus endereços, e a memória comparada com o resultado do porte (com `--casos 8000`):

| Função original | Casos | Diferentes |
|---|---:|---:|
| as constantes depois do construtor estático (0x1fd338) | 1 | 0 |
| `RigidBody::RigidBody` (com `InitLevers`, `VU0_m4toquat`, `VU0_quattom4`, `VU0_MATRIX3x4_mult`, os `WWorldPos`) | 2000 | 0 |
| `ApplyAngularDamping`, `UpdateZonalInfo`, `ControlSleep`, `GetOrientToGround`, `ApplyInitialForcesAndTorques` | 2000 cada | 0 |
| `UpdatePositionAndOrientation` + o microcódigo 0x0ad + `Timestep_CollectVU0data` (1 a 3 corpos) | 1000 | 0 |
| `CollideWithGround` + `TempGetHeightInformation` + `ResolveLeverForces` + 0x034 + `Resolve_CollectVU0data` | 1000 | 0 |
| `GenerateImpulse`, `Util_Bound`, `Util_GenerateCarTensor` | 1000 cada | 0 |
| `ModifyLevers`, `ScaleObjObjForces` (sozinhas) | 3000 cada | 0 |
| `ResolveCollision`: dois objetos, veículo × objeto, dois veículos (donos falsos com tabela virtual na RAM emulada, `GetHitPoints` original, as chamadas a `PlayAnimation` comparadas em ordem) | 1000 cada | 0 |
| `Simulation::SimulateGame`, 6 corpos aleatórios caindo num plano, quadro a quadro | 600 | 0 |

   A conferência inteira roda 35 milhões de instruções MIPS e 10.690 microprogramas originais; o `SimulateGame` dá os mesmos 3.268 choques
   contra o chão dos dois lados. Ficam fora da comparação só os ponteiros (os endereços emulados), a linha 3 do tensor
   (lixo da pilha) e os registradores que o código do EE em modo macro usa como temporários (vf01-vf12, ACC, Q: o porte
   usa variáveis locais e os microprogramas não leem as sobras deles). As chamadas a código não portado são ganchos
   iguais dos dois lados (o chão plano, `memset`, `FastAlloc`, o evento `ECollision`, `CollideWithObject`,
   `CollideWithWorld`, os corpos simples). A conferência achou um erro do porte que a leitura não achou: o limite da
   normal do chão é 0x3f7ff972 (0,9999), não 0,99999.

Os testes sem dados (`tests/test_driving_physics.cpp`, `tests/test_vu_interp.cpp`) conferem propriedades independentes:
o dt truncado; a queda livre segue v = g dt (k - 1) e y = y₀ + g dt² (k - 1) k / 2 (o primeiro quadro sem gravidade);
o limite do momento; o amortecimento; as regras do sono; o impulso de uma alavanca deixa a velocidade normal do ponto
em -e vn e muda o momento angular de r × J; uma normal para baixo não dá impulso; a rotação livre mantém o quatérnio
unitário e avança ω dt por quadro; o limite da velocidade angular; uma caixa para e dorme no chão; `GenerateImpulse`
contra uma parede; em `ResolveCollision`, entre objetos de massas iguais ΔP_A = -ΔP_B e nada gira, entre veículos
m_A ΔP_A = -m_B ΔP_B sem y, corpos se afastando não mudam; os braços de `ModifyLevers` e o corte de
`ScaleObjObjForces`; e, no interpretador, as latências de Q e dos indicadores, o delay slot, o VI backup, a regra da
instrução dupla, o XGKICK e o modo macro.

### Fidelidade

Os portes dão os mesmos bits que o código do PS2 *no interpretador*. O interpretador segue o modelo do PCSX2 (o
interpretador de VU do `VUops.cpp`), com estas aproximações do hardware: o somador do PS2 não tem bits de guarda (uma
subtração cujo menor operando perde bits no alinhamento pode dar 1 ulp acima do truncamento exato daqui) e o
multiplicador (Booth) difere do produto truncado em casos raros: o PCSX2 com "chop" (o padrão) faz como aqui; o
estouro satura em 0x7fffffff (o hardware; o PCSX2 e o interpretador MIPS do port usam ±FLT_MAX, o que nunca acontece
na física); as funções da EFU são as da libm (só a VU1 as usa: `ERLENG` e `ESIN` nos render methods); o XGKICK copia o
pacote na hora. O EE e a VU0 rodam um depois do outro (o `vcallms` roda o programa inteiro): é o mesmo que o PS2 porque
o EE só toca a VU0 de novo com `qmfc2.I`/`ctc2.I`, que esperam o fim.

### O Vanquish

Não foi largado: os parâmetros dele não estão todos nos dados que o porte já lê. O `vanquish.atr` tem a massa (1660) e
os deslocamentos das rodas (`CAR_WHEEL_X_OFFSET` 0,16, `CAR_WHEEL_ZF_OFFSET` -0,894, `CAR_WHEEL_ZR_OFFSET` 1,006), mas
`PBondCar::PBondCar` (0x1862c0) recebe as medidas do carro de quem o cria (o tensor sai de `Util_GenerateCarTensor`,
portado, com essas medidas) e as alavancas além das quatro rodas vêm de `PhysicsObject::GetCollisionGeometry` (os
pontos de colisão do modelo). Falta, então: as medidas e os pontos de colisão (do modelo EAGL ou do CARP, o lado do
desenho), o dono (`PBondCar` com os atributos e as entradas virtuais que a física consulta) e, para o carro andar, as
molas e as rodas (`PBondCar::ProcessPhysics`, 0x18cef8, e `AddWheelForces`, 0x18c1b0: as forças que mantêm a caixa
sobre as rodas). Sem isso o Vanquish seria uma caixa de 1660 kg com quatro alavancas de roda, que o cenário 3 já
cobre (a caixa "do tamanho de um carro").

### A verdade do PCSX2

O interpretador dá os bits do PCSX2 (com "chop") e, pelas aproximações acima, quase sempre os do hardware. Para
conferir o porte contra o jogo de verdade, `tools/pcsx2_pine.py` lê a RAM do PCSX2 pelo PINE (a IPC do PCSX2: ligar
"Enable PINE" nas configurações avançadas; porta 28011) durante uma missão de carro e grava um TSV por quadro da
simulação com cada corpo em uso (o `RigidBody` inteiro em bits, a orientação e o tensor inverso do `RigidBodyInfo`):

```
python tools/pcsx2_pine.py status                       # o PCSX2 responde, o DRIVING.ELF está na RAM (a cadeia
                                                         # "Physics:Rigid" em 0x396338)
python tools/pcsx2_pine.py record out/driving/paris.tsv --frames 600
python tools/pcsx2_pine.py test                          # o protocolo contra um servidor PINE falso
```

Os endereços (o `DRIVING.ELF` do disco americano): a `Simulation` em 0x339190 (+0x1c o vetor dos `RigidBody`, 48 ×
0x80; +0x24 o dos `RigidBodyInfo`, 48 × 0x500; +0x270 os donos; +0xdb4 o estado; +0xdc0 o dt), o contador de quadros
da simulação em 0x355e60 (somado em `ESimFrameUpdate` antes da cópia de volta do scratchpad: o vetor principal tem o
estado do fim do quadro), as constantes em 0x3390cc. O carro do jogador é o corpo de tipo 1 (`RigidBody` +0x6d). O PINE
não para a emulação: o script lê quando o contador muda e descarta a leitura se ele mudar no meio. Com o TSV dá para
(1) conferir um passo isolado: num quadro sem contato com o chão nem colisão, o estado do quadro k + 1 sai do k pelo
`UpdatePositionAndOrientation` do porte (a força e o torque gravados no fim do quadro k são os que o passo seguinte
integra), bit a bit se o PCSX2 estiver com "chop"; (2) quando o carro e o mundo existirem no porte, repetir a mesma
missão com a mesma entrada e comparar quadro a quadro. O script não foi rodado contra um PCSX2 de verdade (só contra o
servidor falso do `test`).

### O que falta na física

- **Colisão entre corpos**: `RigidBody::CollideWithObject` (0x1fbbd8: as caixas orientadas, quem acha o ponto e a
  normal e chama `ResolveCollision`, que já está portado); o conteúdo de `PlayAnimation` (o dono e o modelo).
- **O mundo**: `CollideWithWorld` (0x1fc2f0, os prédios: chama `GenerateImpulse`), `WCollisionMgr`, `WGrid`, o
  `WWorldPos::FindClosestFace` de verdade, o `SurfaceBumpHeight`, e os blocos `CGrd`/`CDat` do CARP que os alimentam.
- **O carro**: `PBondCar` (o construtor, `ProcessPhysics`, `AddWheelForces`), `PVehicle`, os atributos do `.atr`
  (`AttributeSet`), as entradas virtuais do dono.
- **O resto do quadro**: os corpos simples (`SimpleRigidBody`, `DetectCollisionsSRB`), o `DTuningDBMgr` (o porte tem
  `load_rigid_tuning` para o `default.tun`), o `Scheduler` e as tarefas, o evento `ECollision`.

### Efeito no plano

O risco 3 (a física na VU0) cai de "médio" para "baixo" no núcleo: o passo e as forças de alavanca estão portados e
iguais ao PS2 bit a bit, e a ferramenta que provou isso (o EE original e a VU0 original lado a lado com o porte)
serve a cada função nova da física, do carro e das 446 funções com VU0 em macro. O que resta da física da fase 1 é
código de veículo e de mundo comum (C++ do GCC com VU0 em macro), sem microcódigo: o carro (`PBondCar`, `PVehicle`,
~2.000 linhas de MIPS só em `ProcessPhysics` e `AddWheelForces`), a detecção dos choques entre corpos e o mundo. Estimativa: a
física da fase 1 fica em 3 a 4 dias além do que foi feito; o total, de 27-41 para **26-39 dias**, com a incerteza da
física menor (o render e os formatos continuam os riscos maiores).

O próximo passo da física: o chão de verdade (os blocos `CGrd`/`CDat` do CARP de Paris, `WGrid` e
`WWorldPos::FindClosestFace`, conferidos contra o original no mesmo interpretador) e o carro (`PBondCar::PBondCar` com
as medidas do modelo, `ProcessPhysics` e `AddWheelForces`), para o Vanquish andar em Paris; depois `CollideWithObject`
(`ResolveCollision` já espera por ele).

## 12. O nightfire-cxbx

O **nightfire-cxbx** da NightfireResearch (https://github.com/NightfireResearch/nightfire-cxbx) porta a versão de Xbox
rodando o x86 original do `default.xbe` e do `Driving.xbe` e reescrevendo funções em C++; na engine de carro, a física,
a colisão, o mundo, as câmeras, a animação, a entrada e a biblioteca de som SND estão reescritos, e o jogo roda. O estudo
inteiro, com os arquivos deles de onde vem cada coisa e o crédito, está em **`docs/cxbx.md`**; o que muda aqui:

- **Nomes** (`tools/cxbx_names.py`, `tools/xbox_match.py match --cxbx`; `docs/cxbx.md` seção 1): o Ghidra deles do
  `Driving.xbe` tem 6.714 funções com nome, e os lotes deles põem 3.970 nomes do SYM no `DRIVING.ELF` (o europeu, que tem
  o leiaute do nosso). Importados no `out/ghidra_driving`: 2.376 funções novas com nome (antes do `crt0`, de 63% para
  84% dos inícios de função), 71 nomes nossos corrigidos (o alinhamento pelo tamanho do `driving_sym.py` erra nas
  corridas de funções de tamanho igual e nas bibliotecas da EA), 480 globais; os pares Xbox ↔ PS2 sobem de 2.332 para
  3.629 (o código do jogo com par de confiança >= 2: de 57% para 74% dos bytes; a física 92%, a IA 89%, os eventos 69%).
- **Para a física** (`docs/cxbx.md` 2.1-2.3): `CollideWithObject`, `CollideWithWorld`, `WCollisionMgr`, `WGrid`,
  `WWorldPos`, `PBondCar::ProcessPhysics`/`AddWheelForces` em C++ legível, o leiaute do `PBondCar` (0x420), de onde vêm as
  medidas e os pontos de colisão do Vanquish (o `BaseDesc` do CARP), e os blocos `CDat`/`CGrd`/`RNgp` do CARP.
- **Para o render** (`docs/cxbx.md` 2.4): o que o protótipo da seção 10 ainda supõe está no código do jogo deles: a
  `SpecularMat` é o referencial do vetor médio entre a luz e a câmera (conferido no `RLightManager::AddSpecularLight` do
  PS2, 0x1aefb8), falta a luz do chão, o brilho por carro e o `ReflectionInfo2` têm regra própria, o mapa de reflexo é
  feito a cada quadro, e `GAME::SwitchData` sem índice é a entrada 8 no Xbox (o `car_model.cpp` usa a 0; a conferir no
  PS2); a ordem do quadro, as glares, a sombra, o céu e a água.
- **Para o laço e a troca** (`docs/cxbx.md` 2.5-2.6): o `Scheduler` (quatro agendas de oito prioridades, o tempo de 12
  ticks ou mais jogado fora, um bug do original no `timeScale`), a ordem do `GameLoop_StartUp`, e campos novos do bloco
  da seção 4 (+0xa14, já na tabela; +0x5d8, +0x5dc, +0x614, +0x618, +0x624 no lado do `DRIVING`).

**Estimativa** (`docs/cxbx.md` seção 5): de 26-39 para **20-32 dias** (fase 0: 0,5-1; fase 1: 3,5-6; fase 2: 5-8; fase 3:
8-13; fase 4: 2,5-4). A economia está em ler menos: o C++ deles é a versão MSVC/x87, então o porte continua saindo do MIPS
e sendo conferido no interpretador (seção 11); as missões, os eventos, a IA, as armas e o HUD do nível do jogo nenhum dos
dois projetos portou.

## 13. As fases 0 e 1: Paris desenhada e o Vanquish dirigível

O começo do porte das missões de carro (fases 0 e 1 da seção 6), em passos conferidos contra o código original: o CARP
de uma pista resolvido como o jogo, a família de render methods do mundo, a pista desenhada, o chão, o carro e um modo
dirigível no `nf_driving`. Tudo em `src/driving` (namespace `drv`); as funções portadas têm `// [driving] 0x<endereço>`.

### O CARP resolvido (`carp_runtime.h`)

`RCARPFile::Resolve` (0x1a9600) como o PS2: `UGroup::Deserialize` (0x2d61b0) troca as posições relativas por endereços,
o arquivo entra no espaço de nomes "CARP" (`<<Root>>`, `<<Map>>`, `<<Shared>>` e cada `Arti` pelo seu `Name`; o primeiro
vale), os símbolos do modelo EAGL no "EAGL", e `CARP::ResolveSymbolicReferences` (0x2daa18) resolve os registros `sr`
(`USymbolTable::NameLookup`, `UData::AdoptData` 0x2d45d8) e roda os 20 resolvedores por etiqueta de
`CARP::InitResolvers` (0x2da838: `Base`, `as`, `an`, `aq`, `ef`, `di`, `in`/`Cams`, `ps`, `ci`, `Trgr`, `Rule`, `AIEl`,
`AICo`, `AISp`, `Wmap`, `wn`...; `el` só contado: os eventos não foram portados). Em Paris: 2.998 nomes no "CARP", 1.890
no "EAGL"; ficam nulos 21 do CARP, 19 do GIOT, 22 do PHYS e 1 do INSTANCE_ELEMENT (espaços de outros sistemas), e 906
listas de eventos.

### O mundo desenhado (`track.h`, `mat_bond.h`)

`Track` lê a pista como o `WRender::WRender` (0x21eae8): as 2.552 instâncias do `in#0000` do `Map ` (64 bytes: os eixos,
o artigo por `TagReference`, a posição e as medidas empacotadas), os artigos (`as`: os LODs com distância e modelo, a
passada, as bandeiras de render) e as cinco passadas (4 o céu, 3, 2, 1, 0). O desenho segue o `RRenderWorldCamera::DoRender`:
o céu levado ao olho, sem névoa nem luzes pontuais e sem escrever no Z (`RSky::Draw`, `EAGL::RenderContext::SetZWritesEnable`
0x2913f8), as passadas 3 e 2, os carros, a 1 e a 0 (do longe para o perto). O LOD é o do `DrawGroupDrawModel` (0x1ad2e0)
com a distância do `GetInstanceViewDistance` (0x1ad128); todos os artigos de Paris têm um LOD só de distância 1, então o
multiplicador de LOD da vista vale 0 no jogo (o inteiro truncado de `RViewCamera::RefreshLODMultiplier`; a conferir no
PCSX2). A névoa é a do `RFog` com o `.tun` da pista (Paris: começo 1, fim 1450,21, densidade 0,000475, cor 22,15,20).

A família **MatBond** (o `bondrm.o`), lida do microprograma da VU1 e refeita como shader e referência na CPU como a MatCar:
`MatBondTexturePacked`, `MatBondTexture` (entradas em float) e `MatBondReflectTexturePacked` (a passada do mapa de reflexo
primeiro, com o `GlossMapState` e o TAR `bref`, e a da textura). Em Paris ela cobre 7.367 dos 7.541 lotes; conferida no
`ps2::VuMachine` contra o microprograma original (`nf_driving --track paris_mis01 --verify`): 598.518 vértices, 0
diferentes (os 3.847 descartes pelo recorte, ADC, são a divergência documentada). Ficam no substituto
`MatDynamicTexturePacked` (94 lotes), `MatBondScrollTexture2`(+`Packed`) (76) e `MatBondFrameAnimPacked` (4). Paris
carrega em ~0,35 s (1.890 modelos, 244.287 triângulos).

### O chão (`physics/world_collision.h`)

O mundo de colisão é o grupo `CDat` do CARP: as instâncias de colisão `ci` (0x40 bytes: os eixos com as meias-medidas, a
posição no referencial dela, o raio, o artigo), os artigos `ca` (faixas de triângulos com uma esfera cada, barreiras e
janelas), a grade `CGrd` (células de 24 m no plano xz) e os nós `cn` (as listas por tipo de cada célula).
`WCollisionMgr::Init` (0x2132b8) e `WGrid::Init` (0x21c1f8) os montam (`TrackWorld`), e as consultas da física foram
portadas com a aritmética do EE: `WWorldPos::FindClosestFace` (as duas: 0x2322c8 com a face guardada e 0x2324e8 pela lista),
`GetInstanceList`/`GetInstanceListGuts` (0x217588/0x217228: as células pela caixa, `FindNodesBox` 0x21cf80, no máximo
21x21), `FindFaceInCInst` (0x213ff8/0x214338), `FindFaceInTriStrip` (0x2138b0: o teste 2D com as faixas em zigue-zague, o
subtipo 4 ignorado e a máscara de barreiras), `MakeMatrix`, `CalcPosition`, `OrthoInverse`, `MakeFaceAtPoint`,
`SurfaceBumpHeight` (o relevo dos pisos; o `sinf`/`cosf` do newlib não foi portado: `nf::fm`), `GetWorldHeightAtPoint` e
`WWorldMath::GetPlaneY`. Os dois caminhos das contas (FPU, e VU0 com o gerente +0x30 ligado, que a `Simulation` liga na
colisão com os prédios e nos `Simulate` dos objetos) e a máscara de barreiras do gerente (+0x2c: 0xf0 do construtor, 0x20
durante `TempGetHeightInformation` e 0x10 depois) são os do PS2. O arquivo de atrito do disco
(`frictionCoefficients[WSurface_k...]`) não bate com os nomes registrados (`friction[k...]`), então o jogo usa o `.data`
(a grama 0,8, não 0,5): mantido.

**Conferido** (`nf_driving_check`, parte 5): as 8 pistas carregadas pelo porte e a colisão copiada para a RAM emulada; o
código original no interpretador MIPS: `FindClosestFace` (um WWorldPos novo, o que anda pela pista guardando a face, a face
velha), `GetWorldHeightAtPoint`, `MakeFaceAtPoint`, `SurfaceBumpHeight` e as marcas de consulta de cada instância, **0
diferentes** em todas.

### O carro (`game/bond_car.h`, `game/attributes.h`)

O `PBondCar` com o leiaute do PS2 (0x460 bytes: o do nightfire-cxbx até +0x2f0, e daí em diante o do PS2, lido nos
acessores; o `PhysicsObject` com o `WWorldPos` no começo e a tabela virtual em +0x68). Portados: `ProcessPhysics`
(0x18cef8: a ré pelo freio parado, as partes da aderência por eixo, o freio de mão, o contraesterço, o turbo, a marcha e a
tração, a velocidade das rodas, a direção menor com a velocidade, as entradas das rodas com o atrito do piso, a aderência
que cai com a velocidade, a parada no freio de mão, o giro e o escorregar das rodas, a aterrissagem), `AddWheelForces`
(0x18c1b0: as molas, o referencial de cada roda no chão, a aderência de lado até o limite de atrito, a tração, as rampas, o
arrasto do freio de mão, a perda de lado por piso, as travas de rolagem, a estabilidade de guinada), `ImproveLanding`,
`TwoWheelStunt` e `DisableTwoWheelStunt`, `CalculateRPM` (a marcha), `GetControllerInput` (a fila de ações "CarQ"),
`Util_Step`, `IsWheelOnGround`, `InitializeCarVariables`, `InitializeCarControls`, `ResetDamage`, `Util_GenerateMatrix`,
`RSceneObj::GetBoundingDimensions`, `RVehicle::GetRenderOffset`, a parte física do construtor e a de `Simulate` que move o
carro do jogador (o resto de `Simulate`, som, luzes, setas, efeitos, rastro, armas e IA, fica de fora e é contado). Na física
do corpo: `ModifyAngularMomentum`, `GetLocalAngularVelocity`/`Momentum`, `ResolveMassScaledForce4`/`Torque`/`Torque4`,
`SetOrientation`, `ApplyHeavyFriction`; na REAL, `VU0_Sin`/`VU0_Cos` (o seno de grau 9 da VU0 em voltas: erra 0,0033 em
meia volta) e as rotações por eixo. A `Simulation` chama o `Simulate` dos donos depois de mover os corpos, como a árvore de
+0xe68 do PS2.

Achados do PS2 (onde ele difere do Xbox do nightfire-cxbx): a área de rascunho do carro não é a pilha, é o scratchpad
(a área livre da `Simulation` + 0x3af, alinhada), e `Simulate` copia por DMA (canal 9, `toSPR`) o `CarPhysics` para o começo
dela e troca o ponteiro +0x23c pela cópia durante a física; a ordem das contas em float (o Xbox as faz em double) e várias
constantes (a direção divide por 25, o Xbox multiplica por 0,04; o contraesterço multiplica |giro| pela escala e só depois
pela |direção|; a estabilidade de guinada nega antes de multiplicar por 8); o leiaute depois de +0x2f0 (o som em +0x388, a
mancha de óleo em +0x43c, a vida em +0x42c). O `CarPhysics` (0x100 bytes) agora é a estrutura dos atributos do dono na
física inteira (era `PhysicsAttributes` só com os campos usados); os nomes são os do registro de
`PVehicle::InitializeGlobals` (0x1968f8). O sistema de atributos (`AttributeCollection`) lê as coleções `.atr` com as
regras do jogo (a seção da pista, a da coleção e a `[default]`, a posterior vale; a coleção "default" da classe como mãe).

**Conferido** (`nf_driving_check`, parte 6): o Vanquish do `vanquish.atr` do disco num corpo rígido sobre o chão de Paris,
mil estados sorteados (posições sobre as instâncias de colisão, inclinações, velocidades, controles, marcha, turbo, duas
rodas, ré, freio de mão, a face guardada ou não), o código original no interpretador MIPS com a VU0 (a tabela virtual do
carro com as funções originais `GetPhysics`, `IsWheelOnGround`, `DisableTwoWheelStunt`): `ProcessPhysics` (com
`AddWheelForces`, `TempGetHeightInformation` e o chão, `ImproveLanding`, `TwoWheelStunt`), `CalculateRPM` e
`GetControllerInput` comparando o `PBondCar`, o `RigidBody`, o `RigidBodyInfo` e a área de rascunho: **0 diferentes**.

### Dirigindo (`nf_driving --drive`)

`nf_driving --data . --track paris_mis01 --car vanquish --drive` abre uma janela com o carro na largada do
`GameLoop_StartUp` (0x176658: a origem, na altura do chão + 1; `--car-pos x,y,z,giro` escolhe outra, por exemplo a rua
de `-327.7,20,-17.7,0.3155`): os bancos de ajuste da física do disco (`Physics\Rigid`, `Physical`, `Friction`), o teclado
fazendo as vezes do controle do `drivecfg.def` (W/↑ o X analógico, o acelerador; S/↓ o quadrado, o freio e a ré; A/D/←/→ o
direcional; espaço a bola, o freio de mão), a física a 60 passos por segundo (`ESimFrameUpdate`: `SimulateGame` e o
`Simulate` do carro) e a câmera do jogador (seção 14; antes uma provisória do port). `--drive-input "none:1 W:2 W+A:1.2" --screenshot <png>`
roda um roteiro e captura; `--bench-frames N` mede o quadro inteiro (o passo da física, a visibilidade e o desenho, fora
da tela).

Capturas (1920x1080, em `out/shots/drv/` do checkout principal): `drive_paris_reta.png` (2,5 s de acelerador: 33 m/s,
terceira marcha), `drive_paris_curva.png` (virando à esquerda), `drive_paris_freio_de_mao.png`, `drive_paris_re.png`,
`paris_vanquish_parado.png`. Custo do quadro inteiro a 1080p: **4,65 ms (215 qps) na Radeon integrada**, 0,65 ms (1.537
qps) na RTX 5090.

### O que falta da fase 1

- ~~A colisão com os prédios e os objetos~~ e ~~a câmera `RPlayerCamera`~~: feitas, seção 14.
- ~~**A entrada do jogo**: `IOModule::SendGameMessages` lendo o `.def`~~ (feito, seção 14; falta o `InputConfigManager`
  que escolhe o `.def` pelo `Master.def` e o `IFeedback`, a vibração).
- ~~**O laço**: `Scheduler` (as quatro agendas, o `timeScale`)~~ (feito, seção 14) e `GameLoop_MainGameLoop`.
- **O desenho**: a árvore de cenas (`Wmap`/`wn`) e as cortinas de visibilidade (`VisC`), que o jogo usa para cortar (sem
  elas aparecem pedaços do mundo de longe, como a laje com a cidade espelhada sobre o Sena); os métodos
  `MatDynamicTexturePacked`, `MatBondScrollTexture2`, `MatBondFrameAnimPacked`; ~~o vidro do carro
  (`MatCarGlassPacked`)~~ e ~~as luzes (`MatCarTextureSwitchPacked`)~~ (feitos, seção 14) e os outros métodos de carro;
  as rodas (`RVehicle::ComputeWheels`: a compressão, o giro e o esterço); as luzes por quadro. A lista atual e a
  estimativa ficam no fim da seção 14.
- **A missão**: a largada de verdade (as regras e os gatilhos do `SMissionManager` põem o carro na rua), o resto de
  `Simulate`.
- **Fase 0**: os personagens no `--check` (as 8 pistas e os 63 carros carregam).

### Estimativa revista

O que foi feito aqui (o CARP, um método do mundo, o chão e o carro, todos conferidos) é a metade difícil da fase 1. Falta
dela: a colisão com o mundo e os objetos (3-4 dias, com a conferência no interpretador), a câmera (1-2), a entrada e o
laço (1), a visibilidade e os outros métodos (2-3). Fase 1: **7-10 dias** restantes; as fases 2-4 ficam como na seção 12
(15-25 dias), já que a infraestrutura de conferência (as partes 5 e 6 do `nf_driving_check`) serve a elas.

## 14. A fase 1, segunda metade: os choques, a câmera, o vidro, o laço e a entrada

A continuação da seção 13: o Vanquish que bate nas paredes e nos objetos de Paris como no PS2 e a câmera de perseguição do
jogo no lugar da provisória. Tudo portado do MIPS (o PS2 é a verdade) e conferido contra o código original no
interpretador (as partes 7 e 8 do `nf_driving_check`); o Xbox e o nightfire-cxbx (NightfireResearch) só como guia de
leitura (os nomes dos campos e a estrutura das funções grandes).

### Os choques (`physics/collide.h`, `collider.h`, `obb.h`, `world_barriers.cpp`)

- `RigidBody::CollideWithWorld` (0x1fc2f0): o `WCollider` do dono (`WCollider::WCollider`, `Refresh`, `PrepareRegion`,
  `InRegion`, `Validate`, `Clear`; a região é uma esfera um pouco maior que o corpo, puxada para onde ele anda, com as
  instâncias e as barreiras listadas e os carimbos dos artigos), os 11 pontos de um veículo (as 8 quinas, a frente, os
  lados) contra as barreiras (`GetBarrierList` com as instâncias inclinadas no referencial delas, `GetBarrierNormal`,
  `GetClosestIntersectingBarrier`, `SegmentIntersect`, `ClosestCollisionInfo`), os cilindros dos objetos do mundo
  (`GetObjectLists`, `GetClosestIntersectingCylObject`, `GetCylObjectNormal`, `IntersectCircle`) e as caixas (`OBB`,
  `CheckOBBOverlapAndFindIntersection`, `ResolveWorldOBBCollision` 0x1fade8), o empurrão, o impulso (`GenerateImpulse`),
  o dano (`CalculateAndApplyWorldDamage` 0x1fc198), o "encostado na parede" do carro do jogador, os estímulos da animação
  da instância batida e o evento da missão da face 0x0e.
- `RigidBody::CollideWithObject` (0x1fbbd8): as duas caixas orientadas, `ResolveCollision`, o susto (`SetInShock`) e o
  dano nos dois donos. `Simulation::SimulateGame` chama os dois como o PS2.
- Do carro: `PBondCar::SetInShock` (0x187a78), `AddDamageByPlayer` (0x187608) e as entradas pequenas; `ApplyDamage`
  (0x189598) fica contado como pendente (o dano do carro, as peças que caem).

A conferência (parte 7): o código original sobre quatro pistas (as 25.015 barreiras de Paris, os 600 cilindros de
snow1a, as caixas de junglea e os 173 cilindros e 352 caixas de snow2a), com os `WCollider` guardados entre os casos que
seguem andando: os corpos, os `RigidBodyInfo`, os carros, a área de rascunho, os `WCollider`, os eventos `ECollision` na
ordem, as chamadas aos donos e os estímulos, 0 diferentes. A parte 4 roda o `CollideWithObject` original dentro do
`SimulateGame` (147 choques entre corpos, iguais). No `nf_driving --drive` o Vanquish para nas paredes de Paris (de
`-327.7,20,-17.7,0.3155` com 5 s de acelerador: 0,19 m/s encostado no prédio).

### O chão ao longo de um segmento (`world_collision.h`)

A câmera pede ao `WCollider` também as instâncias (a máscara 0xc), e as consultas dela atravessam as faces das faixas de
colisão, o que o carro não usava: `WCollisionMgr::GetInstanceStripList` (0x216ec0: as faixas de cada instância com algum
triângulo perto, pelo raio guardado no w do terceiro vértice; o `GetInstanceListGuts` com as listas), `GetWorldNormal`
(0x2166f8) com o chão (`WWorldPos::FindClosestFace` 0x232638 pelo segmento, `WWorldMath::MakeSegSpaceMatrix` 0x231d68,
`WCollisionMgr::FindFaceInCInst` 0x214c08 e 0x214678, `FindFaceInTriStrip` 0x213d68, a normal do triângulo 0x213bd8,
`WWorldMath::IntersectSegPlane` 0x231af0). Os veículos com `SUB_PHYSICS` também passam por aqui agora.

### A câmera do jogador (`camera/`)

- `dafi.h`: o leitor de INI da EA (`DAFI_open`, `setsection`, `getkeyindex`... 0x2d9408..0x2d9bdc) e o `IniFiles`
  (0x24b9c8..0x24bd60), portados como são: o buffer recortado no lugar a cada `DAFI_setsection`, as chaves e as seções sem
  diferença de caixa, o valor até o fim da linha ou o `;` (um `// comentário` fica no valor: o número é lido do começo).
  Peculiaridades mantidas: um valor vazio engole a linha seguinte. Divergência: uma linha de seção sem `=` (o texto
  depois de um `;`, por exemplo) faz o PS2 girar para sempre; o port pula e conta (o `camera.ini` não tem nenhuma).
- `camera_ini.h`: `RCameraIniLoader::LoadFile` (0x1a49e0) lê o `data/render/camera.ini` do .VIV da missão para o carro
  (`CheckCameraAgainstCar` 0x1a6cf8: a lista "car" da seção, só a primeira ocorrência do nome), nas duas passadas do
  original: as tabelas de modos (0x1c bytes com o ponteiro para membro do GCC 2.95) e as de cada tipo (Heli com os braços,
  Bumper, Spline, Ellipse com as alturas, Fixed, Dashboard, os braços do AutoDrive), os índices dos modos especiais e as
  constantes de `[Global]`, nos leiautes e endereços do PS2. O Vanquish de Paris tem 22 modos, 9 deles Heli.
- `player_camera.h`: o `RPlayerCamera` (0x370 bytes): o construtor e as duas `RestartCamera` (0x1bbc90, 0x1d5538), o
  diretor (`RDirectorQueue::ProcessDirectorLogic` 0x1aa890, `SetCameraModeByIndex`, `DirectorChangeCameraMode`
  0x1ba7a0, `FindHeliArmInd`, `SetupCameraZoom`, as transições), `UpdateCamera` (0x1b29e0) e o modo Heli,
  `UpdateMomentumHeliCam` (0x1b2b48): o braço do modo (o recuo com a velocidade, a vertigem nas ladeiras, o ruído de Perlin
  dos modos do submarino, o braço rígido com o "para cima" suavizado), a âncora (`AnchorCamera`, os `GetAnchor*`), a
  suavização e a transição entre modos, o zoom (`SetCameraZoom`, as duas), a matriz (`SetViewingTransform`); a colisão
  (`ResolveAllCollisions` 0x1b9198: três segmentos do carro para o olho pelo `WCollider` da câmera, a correção pelo
  primeiro choque, o anti-vertigem com o `PointInTriangle`; `CheckObjectCollisions` e `AdjustCamAroundObjectEllipse`: o
  olho sobe pela elipse em volta dos outros carros); o tremor (`CheckForCameraShaking`, `Noise::Init`/`Noise1` com o
  `random()` de 192 bits da biblioteca do PS2).
- O que fica pendente (contado em `camera_pending()`): os outros modos (Bumper, Dashboard, Spline, Ellipse, Fixed,
  Tumble, as animações, o AutoDrive), a câmera do capotamento (o `PBondCar::GetResetAvailable` é zero até o estado do carro
  que o liga ser portado), os pedidos do diretor com outra âncora, as explosões (nenhuma no port), os corpos simples, as
  armas (o gerente de armas: `FindHeliArmInd` e `ResolveWeaponNames` veem nenhuma arma; os braços do Vanquish não
  dependem delas).

`nf_driving --drive` usa a câmera do jogo: o referencial dela vira a matriz de visão e o campo de visão é o do
`RViewCamera` (2 x fov na horizontal da tela 4:3; aqui o vertical dessa tela, Hor+ nas janelas largas). O relatório da
captura ganha `modo braco fov distancia olhar colisoes chao barreiras antivertigem pendentes`.

A conferência (parte 8, `--so camera` roda só ela): o `LoadFile` original com o `IniFiles` e o `DAFI` originais (só a
leitura do arquivo é um gancho) para o "vanquish" e o "vanquishsub" (os modos com ruído, braço rígido e "para cima"),
`Noise::Init`, o construtor, e quadro a quadro o `UpdateCamera` original com a âncora andando por Paris perto das paredes
e do chão (pausas, o olhar para trás, pedidos de outro modo Heli, reinícios, saltos, a máscara de barreiras e o caminho
VU0 do gerente variando): a câmera inteira, o `RPlayerCamState`, os globais do anti-vertigem e do tremor e a fila do
diretor, 0 diferentes (2 x 1.200 quadros por padrão; com `--quadros 1500`, 3.000 por carro: 758 choques com o chão e
1.505 com barreiras, 378 quadros em transição, 609 com o anti-vertigem, 1.502 com o ruído). Ganchos nativos: a biblioteca
C (a newlib do PS2 usa instruções MMI de 128 bits que o interpretador não tem), o `RCamera::RCamera` (as identidades por
PADDW/PEXEW/PEXCW), o scratchpad, a memória, o arquivo e as entradas do carro na tabela virtual.

### O vidro e as luzes do carro (`mat_car.h`)

`MatCarGlassPacked` lido no microprograma (`python tools/vudis.py --eagl bondrm.o MatCarGlassPacked`) e posto na família:
os passos 0-5 da família com outro leiaute (até 40 vértices; os SplitReg em 0x2a, a ModelView em 0x1f, o DmgTexture em
0x23, o State em 0xf5 que o microprograma não chuta e o GlassDmgState em 0xfd que ele chuta nas duas passadas de
textura) e três diferenças:

- o alfa da cor de trabalho é a palavra w da zona de dano do vértice, crua. Quem a escreve é o `RVehicle::ComputeDamage`
  (0x001d3138): a zona = (dano.x, dano.y, 3 × dano.y, o inteiro 127 se dano.x passa do limite, senão 0), copiada para o
  `DamageZones` da VU (0x424620) pelo `SetPerObjectBuffers` (0x001d2b38). Sem dano o w é 0;
- depois da transformação, a face de trás: a normal e a posição pela ModelView e o sinal de n'·p' (a soma na ordem z, x,
  y); com n'·p' ≥ 0 o w do XYZF vira 0x8000 (o ADC, a névoa zerada) e o triângulo que o vértice fecha some; só então o
  AND 0xbffe dos SplitReg;
- quatro passadas: o GlassDmgState com o DmgTexture (`glsd` no Vanquish) e o alfa w; o mesmo estado com o Texture
  (`glas`) e o alfa int(127 − w) (ITOF0, × −1, + 127, FTOI0), o STQ da primeira; o especular e o reflexo da família
  (EnvMapState, `carS`, `bref`) com o alfa 127 − w. O estado é ALPHA 0x44 ((Cs − Cd) × As + Cd) com o teste de alfa
  GREATER 0xa0 e o AFAIL só RGB (o vidro não grava Z). Sem dano o vidro quebrado tem alfa 0 (some) e o vidro inteiro 127;
  quebrado, o inverso. Um laço entre as passadas 2 e 3 faz min(rgb, alfa) com o xyz de registradores velhos (o ITOF0.w só
  converte o w); o que ele grava no xyz é sobrescrito pelo especular antes de qualquer XGKICK, então não muda nada.

`car_reference` faz as quatro passadas; o shader do `nf_driving` recebe o alfa da zona de dano (os 8 bits de baixo, que
são o que o GS lê do A do RGBAQ) e a face de trás pela vista do PC (n'·p' é o mesmo por qualquer rotação: a ModelView do
jogo e a vista do PC dão o mesmo sinal), com o vértice que fecha o triângulo como o provocador do Direct3D
(`car_triangles(..., closing_first)`: (v, v − 2, v − 1), a mesma volta) e o descarte no pixel shader. A conferência
contra o microprograma original (`nf_driving --check --verify`, os 63 carros): 7.285 lotes (267 de vidro), 528.647
vértices, as mesmas 80 cores a 1 e 24 Z de antes, nenhum ADC só na referência, 32.908 vértices com o ADC da face de trás
nos dois lados; os 44 ADC de vidro só no PS2 são do recorte (o w = (F + 2048) × 16). No Vanquish o vidro são 5 lotes e
58 triângulos: o vidro traseiro com o reflexo da cidade, as janelas laterais escuras (`out/shots/drv2/glass_vanquish.png`,
`glass_curve.png`).

`MatCarTextureSwitchPacked` (os faróis e as lanternas: 368 lotes nos 63 carros, 19 no Vanquish) é o
`MatCarTextureDullPacked` com outro leiaute (o `SwitchData` em TOP+0x11 empurra o resto um qword; o `SwitchParam` em
0xf1) e um passo entre as luzes pontuais e a transformação: alvo = (s.z, s.z, s.z, s.y) × 255 × k.x e cor =
int(min(255, cor × (1 − s.w) + alvo × s.w)) nas quatro componentes. O `SwitchData` fica no .bss (0x424520) e o
`RVehicle::SetPerObjectBuffers` grava o w de cada entrada a cada quadro (o RVehicle +0x268: a lâmpada acesa ou apagada);
no port é zero, a lâmpada apagada, e a cor fica a da família.

A conferência roda os dois de novo com valores de jogo na cópia da memória da VU (`variantes`): o vidro quebrado (as zonas
com o w 127, o dano que move e escurece) e a lâmpada acesa pela metade. Nos 63 carros: 8.288 lotes (635 variantes), 577.119
vértices, nenhuma diferença fora das tolerâncias (212 cores a 1, o truncamento da VU; nenhum ADC só na referência; 65.688
com o da face de trás nos dois lados). Sobram no substituto `MatCarGouraudDullPacked` (33 lotes, as cores por vértice de um
`ColourSet`) e `BondCarShadow` (111, que nenhuma instância desenha).

### O escalonador (`game/scheduler.h`)

O `Profile.obj` portado: `Scheduler` (0x179a28 o construtor, `Reset`, `ResetTime`, `Run` 0x179d40), `Schedule`
(0x179fc0, `AddTask` 0x17a158, `RemoveTask`, `RemoveAllTasks`, `EmptiestBucket` 0x17a3d8, `RunTasks` 0x17a4a8) e os
quatro `Process`. Quatro agendas: uma por volta do laço e três no ritmo da simulação (SimRate com um balde, HalfSimRate
com dois, rodando o balde `tick & 1`, QuarterSimRate com quatro, `tick & 3`); uma tarefa nova vai para o balde mais vazio.
Uma tarefa (o `TaskRecord` de 0x1c bytes) guarda o índice do criador do evento (a tabela 0x36fda0), o argumento, a
prioridade, um atraso e uma contagem. O `Run`: os ticks = (relógio − último) × `timeScale`, truncados (`mul.s` e
`cvt.w.s` do EE); 0 não faz nada e deixa o último; 12 ou mais são jogados fora; senão, para cada tick, as 8 prioridades de
cada agenda da simulação, os eventos (`EventManager::RunEvents` 0x175690, o trabalho no destrutor de cada evento) e, no
último tick, as 8 prioridades da agenda por volta e os eventos de novo. Mantido: a fração do `timeScale` se perde (o
último vira o relógio inteiro: 0,75 roda à metade, 1,5 na velocidade de 1); o um-tick-por-volta e o modo das cinemáticas
(ticks seguidos até o modo ser desligado, a agenda por volta a cada 4).

O `RunTheGame` (0x175d78) agenda, na simulação, `ESimFrameUpdate` (0), `EAnimUpdate`, `EAIUpdate` e `ECameraUpdate` (2) e
`ESimEndFrame` (7), e por volta `ERenderFrame` (6) e `EAudioUpdate` (7); o laço é `while (estado != 2) { a fila de ações
global; SYNCTASK_run (a entrada); Scheduler::Run }`. O `nf_driving --drive` roda assim: a entrada antes do `Run`, o relógio
a 60 por segundo, os eventos em fila na ordem em que as tarefas os criam, a simulação, a câmera e o pedido de desenho
ligados ao que já foi portado (os outros contados: o relatório ganha a linha `agenda`); na janela, o quadro só é desenhado
quando o `Run` pede (sem tick, espera o próximo). A captura de antes e a de agora são iguais byte a byte (no Radeon: na RTX
5090 a mesma captura varia entre execuções em ~0,005% dos pixels das fachadas, com ou sem o escalonador, coisa do
driver).

A conferência (parte 9, `--so agenda`): o original (com o construtor, o vetor das agendas e as listas) no interpretador e
o porte com as mesmas operações sorteadas, `--casos` delas: tarefas nas quatro agendas, remoções, `Reset`/`ResetTime` e
`Run`s com o relógio andando de 0 a 15, o `timeScale` (1; 0,75; 1,5; 0,5; 2; 1/3), o um-tick e as cinemáticas (desligadas
depois de alguns eventos). Compara a sequência de criadores chamados e de passagens de eventos, as identidades, os campos
do `Scheduler` e as tarefas de cada balde: 0 diferentes (4.000 casos: 1.417 tarefas, 2.024 `Run`s, 3.114 ticks, 43 com o
tempo jogado fora, 61 cinemáticas). Trocar o `>= 12` por `> 12` ou o primeiro balde vazio pelo último é apanhado.

### A entrada (`game/input.h`)

O `iomodule.obj` e o `actionqueuemgr.obj` portados: o `PS2PadDevice` (0x169288 `PollDevice`: os 32 escalares, os
direcionais `PS2ScaleJoystick` × 1/127,5 − 1 com o y para cima e a zona morta de ±0,3 de `DeadZoneChop`, as 12 pressões
`PS2ScaleAnalogButton` × 1/192 até 1, os 16 botões digitais, o controle digital do tipo 4 que copia os digitais nos
analógicos, os quatro direcionais em zero lidos como "sem analógico"), os predicados do `DeviceScalar` (`isDown`, `isUp`,
as passagens por 0,5, −0,5/0,5 e 1/0, pelo limiar e a volta ao centro), o `InputToAction::LoadParseDefFile` (0x1674d0:
a linha "AÇÃO BOTÃO [MODO] [LIMIAR]", o cabeçalho pulado, o fim no `FRONTEND`/`END`, o limiar 0,5 dos modos > e <, as
117 ações de `getActionID` e os 10 modos U + − > < = I N R M) e o `IOModule::SendGameMessages` (0x168678: os modos que
não repetem só quando o escalar mudou, salvo o "mandar tudo"; a repetição R/M com 300 e 150 ms; o `ACTION_INPUTSYNC` no
fim). Guardas contadas (memória inválida no PS2): o botão que a tabela não conhece (o PS2 grava na tabela −1), a linha
sem `\r` ou vazia.

O `nf_driving --drive` usa o `data\Control\drivecfg.def` da missão: o teclado e o controle do PC fazem um controle do PS2
(W/↑ o X com a pressão inteira, S/↓ o quadrado, espaço a bola, A/D/←/→ o direcional esquerdo nos extremos; o controle do PC
pela posição dos botões, como o controle virtual do jogo a pé) e as ações vão para a fila do carro. A captura com o
roteiro de antes dá o mesmo PNG, byte a byte (no Radeon), com 256 ações em 252 ticks.

A conferência (parte 10, `--so entrada`): os construtores das tabelas de nomes (o `StringToNumber` com a busca binária),
o `PS2PadDevice` e o `InputToAction` originais no interpretador; as tabelas dos dez `.def` do MIS01.VIV (`drivecfg`,
`FrontEnd` e os oito `povcfg`: 215 linhas) iguais às do porte; e 2.400 quadros de controle sorteados (botões seguros por
alguns quadros, direcionais, pressões, o controle digital, o eixo invertido, o "mandar tudo", o relógio) para o `drivecfg`
e o `FrontEnd` (as repetições): as ações (13.067) e os escalares (valores, relógio e estado), 0 diferentes. Mudar a
passagem para cima do direcional de −0,5 para −0,4 é apanhado no quadro 85.

### O que falta da fase 1 e a estimativa (outubro de 2026)

O critério da fase 1 está cumprido: o Vanquish anda em Paris com a câmera de perseguição do jogo, bate nas paredes e nos
objetos como no PS2 (as partes 7 e 8 do `nf_driving_check`) e o quadro inteiro custa 4,49 ms a 1080p na Radeon integrada
(223 quadros por segundo; 0,60 ms na RTX 5090; `--bench-frames 600` com a física, a visibilidade, a cena e o desenho). O
que falta para a fase 1 ficar completa, em ordem:

- **A visibilidade do mundo**: a árvore de cenas (`Wmap`/`wn`) e as cortinas (`VisC`): `RRenderWorldCamera::CullModule`
  (0x1c54a0, a quadtree em 2D e as cortinas), `WRender::GenerateCurtainsAndNodes` (0x21f1d8), `AddCurtain` e o teste
  da esfera no microprograma `VU0_culling` (`vcallms 0x92`); sem elas a laje com a cidade espelhada sobre o Sena aparece
  de longe. 2-3 dias.
- **Os métodos do mundo que faltam** (174 lotes de 7.541 em Paris): `MatDynamicTexturePacked` (94: a família MatBond com
  as três luzes direcionais do lote, TOP+0x10..0x16, sobre as normais em 0x92; lido pela diferença com o
  `MatBondTexturePacked`), `MatBondScrollTexture2`/`Packed` (76, as texturas que rolam) e `MatBondFrameAnimPacked` (4).
  1-2 dias com a conferência.
- **O carro**: `MatCarGouraudDullPacked` (33 lotes nos 63 carros: as cores por vértice de um `ColourSet`) e a sombra
  (`BondCarShadow` com o `RShadowMap`); as rodas: `RVehicle::ComputeWheels` (0x1d2cb0) já lido (o cubo = rotação em y
  pelo esterço limitado a ±0,075 e a altura da mola, a roda = rotação em x pelo giro × o cubo; as instâncias com 0xfe no
  byte +0xd presas ao osso pelo `gProcAnimFunc[0xfe]` (0x1bf600): osso × instância; os nomes dos ossos no `Skel` do
  CARP; os dados no `PBondCar`: `GetCarWheelSpinAngle` +0x230, `GetCarSteer` +0x28c, `GetSuspensionCompression`
  +0x210), o `RSkeletalObj::UpdatePosition` (as peças de mola), e por quadro o `SwitchData` (o w das lâmpadas) e as
  `DamageZones` (`ComputeDamage`). 1-2 dias.
- **A entrada e o laço, o resto**: o `InputConfigManager` (o `Master.def` escolhe o `.def`), o `IFeedback` (a
  vibração), o `GameLoop_StartUp` com a largada da missão (as regras e os gatilhos do `SMissionManager`, que já são da
  fase 2).
- **Fase 0**: os personagens no `--check`. Meio dia.

Fase 1: **5-8 dias** restantes (eram 7-10 antes desta etapa). As fases 2-4 ficam como na seção 12 (15-25 dias): a
conferência no interpretador (agora com as partes 9 e 10 do `nf_driving_check`) serve a elas.

## Ferramentas

| Ferramenta | O que faz |
|---|---|
| `tools/driving_sym.py` | lê o `DRIVING.SYM` (`info`, `list`, `objs`, `modules`, `classes`), casa com o ELF (`match --evaluate`) e gera os nomes para o Ghidra (`ghidra`); `test` |
| `tools/xbox.py` | a cópia da versão americana de Xbox, só como referência de leitura (fica em `xbox/`, no `.gitignore`): `info` (partição, arquivos, os XBE e o certificado: título, região, versão, o caminho do build, as bibliotecas do XDK), `list`, `extract <imagem> <pasta>` (`.xiso` ou a imagem inteira do disco), `xbe`, `pe <xbe> <exe>` (o PE32 equivalente para o Ghidra e os rótulos: os thunks do kernel, as seções do XDK), `vsh <objeto.o> <método>` (o programa de vértice do NV2A de um render method de Xbox), `test` |
| `tools/ghidra_xbox.sh` | o projeto headless `out/ghidra_xbox` do Xbox (`pe`, `import`, `names`, `functions`, `features`, `decomp`, `disasm`, `callers`) |
| `tools/xbox_match.py` | casa as funções do Xbox com as do PS2 (`match --evaluate`, `show`, `test`); `out/xbox/match.tsv` e `xbox_names.tsv`; `match --cxbx` usa também os pares pelo nome do nightfire-cxbx (seção 12) |
| `tools/cxbx_names.py` | os nomes do nightfire-cxbx (NightfireResearch) nos nossos projetos do Ghidra (seção 12, `docs/cxbx.md`): `compare`, `pairs` (`out/xbox/cxbx_pairs.tsv`), `names` (`out/driving/cxbx_names.tsv`, `out/xbox/cxbx_xbox_names.tsv`), `test` |
| `tools/ghidra_driving.sh` | o projeto headless `out/ghidra_driving` (`import`, `import-base`, `functions`, `features`, `names`, `decomp`, `disasm`, `base`) |
| `tools/ghidra_scripts/ApplySymbolTable.java` | aplica uma tabela de nomes (funções, comentários, rótulos; o tipo `A` acrescenta uma linha ao comentário de cabeçalho) |
| `tools/ghidra_scripts/ExportMatchFeatures.java` | por função: cadeias, constantes, globais, chamadas em ordem; as tabelas de ponteiros (o casador) |
| `tools/ghidra_scripts/XboxVtables.java` | as tabelas virtuais do MSVC sem RTTI e as funções que só elas apontam |
| `tools/vudis.py` | também `DRIVING.ELF --prog=N` (os três programas da VU0) e `--eagl <objeto.o> <método>` (o microprograma da VU1 de um render method do PS2) |
| `tools/driving_data.py` | os BIGF da pasta `DRIVING` (`stats`, `list`, `extract`, `ssh`, `carp` (a árvore de UGroup por caminho), `asf`, `eagl`); `test` |
| `nf_driving` | o desenho (seção 10): um carro girando numa janela (`--data`, `--viv`, `--model`, `--adapter`); `--screenshot` (fora da tela, com o relatório `.txt`: métodos, peças, instâncias, texturas, o estado do GS de cada passada); `--verify` (cada lote da família no interpretador da VU1 contra a referência); `--check` (todos os carros de todos os arquivos e as 8 pistas com o mundo de colisão; `--check-viv`); `--track <pista>` (a pista desenhada, com `--car`, `--car-pos`, `--cam`, `--verify`); `--drive` (o carro dirigível, seção 13: `--drive-input`, `--seconds`); `--census` (os 115 render methods por microprograma e família); `--view`, `--part`, `--no-spec`, `--no-refl`, `--no-fallback`, `--bench-frames` |
| `tools/ghidra_scripts/ExportDecompiledAt.java` | decompila funções pelo endereço (cria a função se o Ghidra não a tem), sem gravar no projeto |
| `nf_driving_check` | a física conferida contra o `DRIVING.ELF` (seções 11 e 13): a cobertura do interpretador das VUs (os três programas da VU0 e os 115 render methods), o microcódigo original × o porte à mão, o passo inteiro dos dois jeitos, as funções originais do EE no interpretador MIPS com a VU0 × o porte; a parte 5 (o chão das 8 pistas), a 6 (o carro sobre o chão de Paris), a 7 (os choques, seção 14), a 8 (a câmera; `--so camera` só ela), a 9 (o escalonador; `--so agenda`) e a 10 (a entrada; `--so entrada`); `--consultas N` |
| `tools/pcsx2_pine.py` | grava os corpos rígidos de uma missão de carro a cada quadro pela IPC do PCSX2 (`status`, `record`, `test`) |
