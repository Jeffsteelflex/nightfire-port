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
- O **mundo vivo** (seção 18): a rede de estradas de Paris, o tráfego (os 20 carros da IA, o motorista e a física deles),
  os 754 gatilhos da pista com os eventos que põem o tráfego na rua e os 544 objetos de cena que o carro derruba (o
  `Smackable`), tudo igual ao código original no interpretador (a parte 14 do `nf_driving_check`); fases 2-4: 13-23 dias.
- A **missão** (seção 19): o `SMissionManager` de Paris com as 114 regras, os eventos (a imagem do objeto do PS2 de 97
  números, os destrutores da missão e do HUD), os textos do `.loc` e um HUD mínimo; o `nf_driving --drive` começa Paris
  onde o jogo começa, roda as regras e mostra os objetivos, as mensagens e o resultado, igual ao código original no
  interpretador (a parte 15). Ganhar exige a IA inimiga; fases 2-4: 14-24 dias (a seção do helicóptero e os filmes
  entraram na conta).
- Os **personagens** (seção 20): o render method de pele lido no microprograma da VU1, a biblioteca de animação da EA (os
  canais dos 89 bancos, o esqueleto) e o `ActPoser`, iguais ao código original nos interpretadores (a parte 16 do
  `nf_driving_check` e o `--verify`); o `nf_driving --actor` desenha um personagem de Paris tocando uma animação; fases
  2-4: 13-22 dias, somada a missão.
- As **armas ligadas à IA** (seção 24): a mira pela câmera, a trava nos alvos (`WTargetPicker`), as balas (`RayShell`,
  `Shell`, as janelas), o `EFireWeapon` inteiro, a direção automática e as armas dos motoristas da IA, a camada dos
  atores e as mãos do Bond com o fuzil, iguais ao original no interpretador (a parte 20 do `nf_driving_check`); Paris se
  joga do começo ao fim sem substitutos (o atirador e o piloto de depuração só apertam os botões do controle): os pneus
  dos caminhões estourados pelo fuzil, a van parada pelo Q-Pulse, "Mission Accomplished"; faltam os passageiros que
  atiram no jogador; Paris 7-12 dias, fases 2-4 7-13.
- Os **efeitos desenhados** (seção 25): a galeria dos efeitos e o sistema de partículas, as luzes das explosões e dos
  mísseis, as marcas no chão, o rastro dos mísseis e os eventos de efeito, do MIPS, iguais ao original no interpretador
  (a parte 21 do `nf_driving_check`), e os dois render methods deles (`RenderSpritesRot`, `MatDebugTexture`) iguais ao
  microprograma no `--verify`; o `nf_driving --drive` desenha o fogo, a fumaça, a luz e a marca das explosões.

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
`MatDynamicTexturePacked` (94 lotes), `MatBondScrollTexture2`(+`Packed`) (76) e `MatBondFrameAnimPacked` (4) (feitos
na seção 15). Paris
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
- **O desenho**: ~~a árvore de cenas (`Wmap`/`wn`) e as cortinas de visibilidade (`VisC`)~~ e ~~os métodos
  `MatDynamicTexturePacked`, `MatBondScrollTexture2`, `MatBondFrameAnimPacked`~~ (feitos, seção 15); ~~o vidro do carro
  (`MatCarGlassPacked`)~~ e ~~as luzes (`MatCarTextureSwitchPacked`)~~ (feitos, seção 14) e os outros métodos de carro;
  as rodas (`RVehicle::ComputeWheels`: a compressão, o giro e o esterço); ~~as luzes por quadro~~ (as do mundo, seção
  15). A lista atual e a estimativa ficam no fim da seção 15.
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

- ~~**A visibilidade do mundo**: a árvore de cenas (`Wmap`/`wn`) e as cortinas (`VisC`)~~ (feita, seção 15).
- ~~**Os métodos do mundo que faltam** (174 lotes de 7.541 em Paris)~~ (feitos, seção 15).
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

Fase 1: **5-8 dias** restantes (eram 7-10 antes desta etapa; 3-5 depois da seção 15). As fases 2-4 ficam como na
seção 12 (15-25 dias): a conferência no interpretador (agora com as partes 9 e 10 do `nf_driving_check`) serve a elas.

## 15. A fase 1: a visibilidade do mundo e os métodos que faltavam

A metade do mundo do que a seção 14 deixou para a fase 1: o corte da pista como o jogo o faz (a árvore de cenas, as
cortinas de visibilidade e os objetos de cena) e os quatro render methods do mundo que ainda iam pelo substituto, com as
entradas por quadro que eles leem. Tudo portado do MIPS do PS2 (a verdade); o Xbox e o nightfire-cxbx (NightfireResearch:
`src/driving/world/Render.cpp`, `VisCurtain.cpp`, `render/WorldCulling.h`, e o nome `RLightManager::SetLightDirections`
da função 0x1b0038, que o DRIVING.SYM não tem) só como guia de leitura.

### A visibilidade (`world_vis.h`, `vu0_culling.h`)

O que o `RRenderWorldCamera` faz antes de desenhar a pista, em quatro passos:

1. `RPlayerViewCamera::ConfigureView` (0x1beba0) monta o `fgWorldCulling` (`RRenderWorldCulling`, 0xa0 bytes em
   0x334160, construído na partida com campo 33 e alcance 400 pelos construtores globais 0x1d4fc0): um "frustum" em 2D
   no plano xz (`Setup2dFrustrum` 0x1c6858 com o `InitializePlaneInfo` 0x1c6b70): as duas bordas pelo ponto mais longe,
   os dois lados e o de perto pelo olho. O alcance é o `CullDistance` (400 m; × 1,1 com o campo abaixo de 32 graus), o
   campo × 1,25 em tela larga (`fgRenderer` + 0x5c), e o olho recua |frente.y| × alcance × 0,2 quando a câmera olha para
   cima ou para baixo. Os quatro primeiros planos ficam transpostos (a coluna k é o plano k), como a VU0 os lê.
2. `WRender::GenerateCurtainsAndNodes` (0x21f1d8, com o `FindVisibleTreeNodesAndCurtains` 0x21ef68) desce a árvore de
   cenas do `Wmap` (aberta pelo `WWorld::Open` 0x22faf8 no `WWorld` + 0x1c: a raiz, o canto e o tamanho; os nós
   `CARP::MapNode` de 0x10 bytes com os quatro filhos de `wn` e a lista de índices das instâncias;
   `WTree::InitializeTree` 0x22c730) contra o frustum (`QuadtreeFrustrumCheck2d` 0x1c66c8: fora, dentro, cruzando; o raio
   = o w da caixa do nó × 1,414) e guarda os nós visíveis na lista do scratchpad do EE (`CachedDrawInfo`, 0x2c08
   bytes em 0x70000000: 0x300 nós e 0x400 desenhos {instância, distância}). As cortinas (`VisC`, `WVisCurtain` 0x22f660
   com o `CalcRadius` 0x22f428: paredes verticais de duas pontas) dos nós visíveis que caem no frustum viram cortinas
   ativas pelo `AddCurtain` (0x22f4a0): até 63, e a partir da segunda só se o pé de uma das pontas (raio 0,1, margem
   0,1) não estiver escondido por uma já ativa.
3. `RRenderWorldCamera::CullModule` (0x1c54a0) anda pelas instâncias dos nós visíveis (`WRender::GetNextPoint`
   0x21f2b8: a esfera da instância pelas medidas empacotadas em +0x3c, o raio = o x × o passo do bit 30, a altura = o y
   à parte ou o dobro do x; a bandeira 8 em +0xc tira o corte no longe, a 0x40 o põe com a escala 0,3 ou 2) e guarda
   (`SetCull` 0x21f570) as que ficam no frustum (`IsInFrustum2d` 0x1c65b0: o plano de perto, o longe a escala × o
   alcance e os dois lados) e que nenhuma cortina esconde (`IsVisibleAgainstCurtains` 0x22f5a8; com o corte no longe a
   esfera vai sem raio e a margem é a altura).
4. `DrawWorld` desenha o céu (`RSky::Draw` 0x1ece18) e as passadas 3 e 2 ordenadas pela instância; `DoRender` copia
   as passadas 0 e 1 (`CopyDrawPasses` 0x21f5b8) e as desenha depois dos carros, do longe para o perto. A ordem de cada
   `WRender::DrawPass` (0x21f670) é o `std::sort` do GCC 2.95 que o DRIVING.ELF compilou (`__introsort_loop` 0x2207c0:
   o limite 16, a mediana de três, o `partial_sort` na profundidade zero; `__final_insertion_sort` 0x220b10), com os
   comparadores `lessThanByInstance` (0x220bc0, sem sinal) e `greaterThanByDistance` (0x220bd0): com distâncias iguais a
   ordem é a do introsort, por isso ele foi portado em vez de um `std::sort` qualquer.

O microprograma `VU0_culling` (o terceiro do `.vutext`, 189 instruções, enviado por `UploadCullingMicrocode` 0x22f3e0 a
cada quadro de desenho) foi portado à mão como o `VU0_physics`: o ponto 0x000 grava a cortina (os quatro planos
transpostos na memória de dados da VU0, em 4 × o índice: a parede, os planos do olho pelas duas pontas e o de trás;
nada se o olho está atrás da parede), o 0x092 `CheckAgainstVisCurtains` testa a esfera e a margem contra as cortinas
ativas (os quatro bits de sinal do MAC pelo `FMAND 0xf0`), o 0x0a8 `CheckPositionAgainstVisCurtains` um ponto. A física
e as cortinas dividem a VU0 como no PS2 (`drv::vu0()` e o mesmo modo, Native ou Interpreted).

As estruturas têm o leiaute do PS2 (ponteiros como palavras p32) e os globais do EE são globais aqui também
(`fgRender`, `CurrentList`, o cursor, `numActiveCurtains`, o mapa do `WWorld`), para a conferência byte a byte.
Divergências, documentadas nas funções: o `atan2f` do newlib (`Setup2dFrustrum`) é o `nf::fm::atan2`; um nó além do
limite de 0x300 da lista não entra e conta em `world_vis_guards()` (o PS2 não confere: memória inválida; 0 nas 8
pistas); o `QuickDrawInstance` vira uma lista de instâncias na ordem do jogo (`DrawPass_order`), que o `nf_driving`
desenha (sem as instâncias com a bandeira 1, que são dos objetos de cena, abaixo). Só do PC: numa janela mais larga que
4:3 o campo do corte cresce como o desenho (Hor+, `cull_fov_for_window` no `nf_driving`); o jogo corta pelo campo de
4:3. `--no-cull` volta ao corte de antes (só o volume de visão).

O `Track` (`track.h`) lê o `Wmap` na carga e `Track::cull(câmera, campo, tela larga, saída)` devolve as listas na ordem
do `DoRender` (o céu, o mundo das passadas 3 e 2, os objetos de cena, os desenhados por último junto com os carros, a
passada 1 e a 0); o `nf_driving --drive` e o `--track` desenham por elas, com a câmera do jogador ou a livre, e o
relatório ganha `corte nos N cortinas N testadas N guardadas N | ceu mundo objetos_de_cena visíveis/por_último de N
passada1 passada0 | guardas N` (e `corte_ms` no `--drive`).

A laje com a cidade espelhada que aparecia de longe sobre o Sena (seção 14) era a camada de telhados `RT_Low_*` vista
de baixo, da origem da missão (o carro a 2 m de altura, abaixo do nível da rua): ela fica dentro dos 400 m e o corte
fiel a guarda; da largada na rua ela não aparece. Paris pela câmera do jogador na largada (`--car-pos
-327.7,20,-17.7,0.3155`, 1 s depois): 36 nós, 6 cortinas, 703 instâncias testadas, 203 guardadas e 18 dos 557
objetos de cena, e a imagem igual à do `--no-cull` dentro do ruído das capturas (140 de 921.600 pixels; duas capturas
iguais já diferem em 90 a 145 pixels, também no main: o desenho do `nf_driving` não é determinístico no último pixel; o
que o corte tira estava escondido); de cima (a vista geral) ficam os prédios a menos de 400 m, as janelas e luzes com a
bandeira 8 (sem corte no longe) e as instâncias 0x48 (800 m).

### Os objetos de cena (`RSceneObj`)

Os postes, as placas e o resto do que o carro derruba não estão na árvore: 550 das 2.552 instâncias de Paris nenhum nó
lista (a bandeira 0x10, quase todas 0x50). O `WWorld::Open` (0x22faf8) faz de cada instância com a bandeira 0x10 cujo
registro do `ps  ` (0x20 bytes pelo u16 da instância em +0xe) tem o byte +7 diferente de 0xff um objeto de cena: a
instância ganha a bandeira 1 (o `QuickDrawInstance` deixa de desenhá-la) e, com artigo, um `RSceneObj`
(`RSceneObj::RSceneObj` 0x1c7098: a esfera da posição da instância com o raio das medidas, 10 se zero, e o nó mais
fundo que a contém, `WTree::FindNode` 0x22c1a8 a partir do quadrado do `Wmap`, onde o `SetMapNode` 0x1c8cf8 o põe no
começo da lista do nó, o +4 que o `InitializeTree` zera). O `PerformCulling` corta os objetos depois do mundo com o
mesmo `CullModule` e o iterador deles (`PrepareSceneObjsForCulling` 0x1c80f0, `GetNextSceneObjCullInfo` 0x1c8130: os
objetos de cada nó visível; `SetSceneObjectCull` 0x1c8490 e `SetNowVisible` 0x1c8c70: as listas de visíveis, até 0x200,
e a dos desenhados por último, com o bit 27 das bandeiras), e o `DoRender` os desenha depois do mundo e antes dos carros
(`RSceneObj::RenderAllNormal`; os por último no `DrawVehiclesAndDeferredSceneObjects`). Em Paris são 557 objetos. O
`RSceneObj` em si (o modelo próprio do `UseArticle`, a física quando o carro bate, os efeitos, o `Render`) não foi
portado: o port desenha a instância de origem no lugar dele; o `RPathEngine::GetPathHandle` dá 0, os atores
(`ActActorDatabase`) e a galeria de efeitos (`GFXGallery`) ficam de fora do `PerformCulling`. Sem os objetos de cena o
corte apagava os postes e as placas que o desenho de antes mostrava.

### Os métodos do mundo que faltavam (`mat_bond.h`)

Os 174 lotes de Paris que iam pelo substituto, lidos do microprograma da VU1 e refeitos na família MatBond (shader do
D3D11 e referência na CPU, `bond_reference`), com as entradas novas do lote:

- `MatDynamicTexturePacked` (94 lotes): a cor de partida (127, 127, 127, 128: o `vf00 + 127` com o w de `vf00` = 1) e
  as três luzes do `LightSources` do lote (TOP+0x10..0x16: as direções transpostas nas linhas 0-2, as cores das três
  luzes e o ambiente): M = as linhas xyz da `WorldMatrix` × L, d = max(0, n × M) com as normais em float (TOP+0x92),
  cor = int(min(255, 127 × (C0 d.x + C1 d.y + C2 d.z + C3))), alfa 128; depois as luzes pontuais e a transformação
  como os outros.
- `MatBondScrollTexture2Packed` e `MatBondScrollTexture2` (38 lotes cada; o céu de nuvens e a água do Sena): duas
  passadas, o `Texture` com a STQ + a fração de `ScrollUV × t` e o `Texture1` com a segunda STQ + a de `ScrollUV1 × t`
  (t = `SimStep.x`; a fração cortada para o zero, com o sinal do produto); a segunda STQ do Packed **não** é convertida
  (fica a palavra que o VIF deixou, lida como float: nos arquivos ela já é float, z = 1); o `State1` nunca é chutado.
- `MatBondFrameAnimPacked` (4 lotes): o t da STQ + `FrameAnim.y × int(frac(FrameAnim.x × (SimStep.x - SimStep.w)) ×
  FrameAnim.z)` (a tira de quadros da textura).

As entradas por quadro (`light_manager.h`): o `GAME::SimStep` é o `RTimeData` (0x334000: o x é o passo da simulação
sem sinal em float, `RTimeData::Update` 0x1d1ef8, chamado no começo de cada desenho; o .data dá (0, -0,002, 0, 0)) e o
`GAME::LightSources` é o bloco de luzes do `RLightManager` (0x70 bytes): o construtor (0x1ae630) lê as cores e os
ângulos dos quatro conjuntos do `data\tuning\Render\Lighting\<pista>.tun` da missão, o `RefreshLightBlocks` (0x1b0158)
monta cada bloco (as direções por `SetLightDirections` 0x1b0038: (0, 0, 1) girado pelo ângulo X em volta de x e pelo Y
em volta de y, com `BuildRotate` 0x2d11c8 e `TransformPoint` 0x2d1920; a "Dual Hemi" apontando para cima no World e no
Character) e o `SetLightingModel` (0x1aec20) copia o do modelo para o `LightSources`. Divergências: o `DTuningDB` da EA
não foi portado (as chaves do `.tun` são lidas direto do texto) e o `sinf`/`cosf` do newlib é o `nf::fm`. O renderizador
do PC calcula por desenho o deslocamento da rolagem e o do quadro (`bond_scroll_offset`, `bond_frame_offset`, com a
aritmética da VU) e passa as luzes no `cbuffer` do quadro; os vértices do Dynamic e dos Scroll2 levam um segundo fluxo
(a segunda STQ e a normal), o dos outros fica com 44 bytes.

A conferência (`nf_driving --track <pista> --verify`): cada lote da família no `ps2::VuMachine` com o microprograma
original contra a referência, e o Dynamic, os Scroll2 e o FrameAnim de novo com outras luzes e um `SimStep` de jogo
longo (12.345 e 1.234.567 passos) nas `variantes`, para passar pelas contas que o quadro 0 deixa triviais. Nas 8
pistas: 31.851 lotes (com as 1.755 variantes), 1.804.944 vértices, 0 diferentes (STQ, cor, posição, névoa); Paris
sozinha 7.795 lotes, 625.892 vértices, 0 substitutos (eram 174). Fora de Paris ficam no substituto os métodos que ela
não usa: `MatBondScrollTexturePacked`/`MatBondScrollTexture` (a rolagem de uma camada), `MatBondScrollTexture3Packed`,
`BondGouraud` e os da água e do fundo do mar de `uw_mis11` (`BondUnderWater(Packed)`, `BondKelp`, `MatBondWater(Packed)`:
5.369 dos 5.644 lotes).

### A conferência da visibilidade (`nf_driving_check`, parte 11)

`nf_driving_check <dados> --so visibilidade` (e a parte 11 da rodada inteira), o código original no interpretador MIPS
com a VU0 contra o porte, byte a byte:

- o `VU0_culling` original no `ps2::VuMachine` contra o porte à mão (o estado inteiro da VU0: registradores, ACC, Q, VI,
  a memória de dados) nos três pontos de entrada;
- `RRenderWorldCulling(33, 400)`, `Setup2dFrustrum`, `IsInFrustum2d`, `QuadtreeFrustrumCheck2d`, o `WVisCurtain`, as
  sequências de `AddCurtain`/`IsVisibleAgainstCurtains`/`CheckPositionAgainstActiveCurtains` e o introsort com
  entradas sorteadas;
- as 8 pistas: a árvore copiada para o interpretador, o `WTree::InitializeTree` e as passadas do `WRender`, e câmeras
  andando por cada pista (`ConfigureView`, `GenerateCurtainsAndNodes`, `PrepareForCull`, `CullModule`: o frustum, o
  multiplicador de LOD, as cortinas e os planos na VU0, a lista do scratchpad e o cursor), o `DrawPass_sort` das listas
  e o `CopyDrawPasses`; os objetos de cena: o construtor original do `RSceneObj` (`WTree::FindNode`, `SetMapNode`) para
  cada objeto que o porte criou, comparando os objetos e as listas dos nós, e a cada quadro o corte deles
  (`PrepareSceneObjsForCulling`, `GetNextSceneObjCullInfo`, `SetSceneObjectCull`/`SetNowVisible`);
- as entradas por quadro: o `RTimeData::Update` (também os passos negativos, lidos sem sinal) e as direções do bloco de
  luzes com ângulos sorteados e os dos `.tun` das 8 pistas.

`--casos 4000 --quadros 600`: 1.334/1.333/1.333 casos do microcódigo, 4.000 frustums, 16.000 testes de esfera e 16.000
de quadrado, 45.641 operações de cortina, 4.000 ordenações, 2.400 quadros nas 8 pistas (por quadro 27,0 nós, 2,1
cortinas, 557,6 instâncias testadas e 83,8 guardadas; nenhum nó além do limite), 4.800 listas ordenadas, 9.600 cópias,
1.793 objetos de cena construídos nas 8 pistas e 2.400 quadros do corte deles (5,2 visíveis por quadro), 4.000
`RTimeData` e 4.032 blocos de luzes: 0 diferentes. Para rodar o código original o interpretador MIPS ganhou o
scratchpad do EE (16 KB em 0x70000000), o acumulador da FPU (`ADDA.S`, `SUBA.S`, `MULA.S`, `MADD.S`, `MSUB.S`,
`MADDA.S`, `MSUBA.S`) e as MMI `PADDW`, `PEXEW` e `PEXCW`; o `memset` (outras MMI) e o `RPathEngine::GetPathHandle` (0)
são ganchos nativos.

### O custo e as capturas

O quadro inteiro (`--drive --car-pos -327.7,20,-17.7,0.3155 --bench-frames 600`, 1920x1080: o passo da física, o
corte, a cena e o desenho; três rodadas alternadas com o executável do main, na mesma hora): **6,02 ms (166 qps) na
Radeon integrada** contra 6,47 ms do main, e 0,75 ms na RTX 5090 contra 0,89 ms. O corte (o mundo e os objetos de cena)
custa ~0,55 ms de CPU por quadro (`corte_ms`: a VU0 em C++ com a aritmética do PS2) e no fim da medida (o carro 50 m
adiante) deixa 204 itens e 537 chamadas de desenho onde o main desenhava 573 e 1.319; isso paga com folga os 174 lotes
que agora têm o método certo e a segunda camada de nuvens do céu. (Os 4,49 ms da seção 14 foram medidos em outra hora:
o main mede hoje 6,47 ms nas mesmas condições.)

Capturas (em `out/agent_drv_world/shots/` do checkout principal): `street.png` (a largada na rua, 1080p, pela câmera do
jogador), `street_cull.png`/`street_nocull.png` (a mesma vista com e sem o corte, 1280x720: iguais dentro do ruído),
`far_cull.png`/`far_nocull.png` (a vista geral de cima: o corte deixa os 400 m em volta, sem ele a cidade inteira),
`start_cull.png`/`start_nocull.png` (a origem da missão, onde a laje de telhados aparece de baixo nos dois),
`sky_t0.png`/`sky_t300.png` (as nuvens do céu, um `MatBondScrollTexture2`, andam com o `SimStep`: 47% dos pixels mudam
em 300 passos), `logo007.png` (um dos logotipos 007 da pista, `MatDynamicTexturePacked` aceso pelas luzes do `.tun`) e
`verify_<pista>.png` das 8 pistas.

### O que falta da fase 1 e a estimativa

Da lista do fim da seção 14 saem a visibilidade e os métodos do mundo. Ficam:

- **O carro**: `MatCarGouraudDullPacked` e a sombra (`BondCarShadow` com o `RShadowMap`), as rodas
  (`RVehicle::ComputeWheels`), o `SwitchData` e as `DamageZones` por quadro. 1-2 dias.
- **Os objetos de cena**: o resto do `RSceneObj` (o `UseArticle` com o modelo próprio, o `Render`, a física quando o
  carro bate: o `Smackable` e o `Simulation::SpawnSmackableObject`), o `RPathEngine` e as animações das instâncias
  (`gProcAnimFunc`, os `ProcAnimInstanceAndPointer` do `WWorld::Open`). 1-2 dias (a física dos objetos é da fase 2).
- **Os métodos das outras pistas**: a rolagem de uma camada (`MatBondScrollTexture(Packed)`, a da selva),
  `MatBondScrollTexture3Packed`, `BondGouraud` e os do fundo do mar (`BondUnderWater(Packed)`, `BondKelp`,
  `MatBondWater(Packed)`). Da fase 1 (Paris) não são; 1-2 dias quando as outras missões vierem.
- **A entrada e o laço, o resto**: o `InputConfigManager`, o `IFeedback` (feitos na seção 16), o `GameLoop_StartUp` com a
  largada da missão (fase 2).
- **Fase 0**: os personagens no `--check` (feitos na seção 16).

Fase 1: **3-5 dias** restantes (eram 5-8). As fases 2-4 ficam como na seção 12 (15-25 dias).

## 16. A fase 1: o carro desenhado (as rodas, a sombra, as luzes), a vibração, o Master.def e os personagens

A parte do carro do que faltava da fase 1 (seção 14, "O que falta"): o Vanquish com as rodas girando e virando, a sombra,
os faróis acesos e o dano por quadro, a vibração no controle e a escolha da configuração do controle pelo `Master.def`,
e os personagens no `--check` (a fase 0). Tudo portado do MIPS do PS2 e conferido contra o código original no
interpretador (a parte 12 do `nf_driving_check`, `--so carro`) ou contra o microprograma da VU1 (`nf_driving --check
--verify`); o Xbox e o nightfire-cxbx (NightfireResearch) só como guia de leitura.

### O Gouraud e o BondCarShadow (`mat_car.h`)

`MatCarGouraudDullPacked` (33 lotes nos 63 carros, 2 no Vanquish) lido no microprograma: o `MatCarTextureDullPacked`
com a cor de cada vértice tirada de um `ColourSet` (uma LQI de TOP+0xbe pelo índice do lote, depois da conversão do passo
1 sobre a área de luz, que o port refaz numa cópia da memória: a mesma cor para o lote todo), a luz direcional vezes 127
(não a cor de partida), as luzes pontuais somadas, vezes a cor com o dano e / 128, sem limite (as cores lixo além do
`ColourSet` passam de 255; o GS lê os 8 bits de baixo) e o alfa o w cru. `BondCarShadow` (111 lotes): a posição pela
MyMatrix, o Q, o FTOI4 da tela e o w 2047 quando o FCAND dos três últimos CLIP dá algo (o triângulo sai), a cor o
`CarShadowColour`, a saída em 2 qwords por vértice (`CarMethodLayout::out_qw`). Os dois na `car_reference`, no shader do
`nf_driving` (o Gouraud com a cor crua num `uint4`) e no `--verify`, com variantes (o dano, as luzes pontuais; a MVP em
perspectiva na sombra): nos 63 carros 8.572 lotes (777 variantes), 604.135 vértices, as mesmas 212 cores a 1 e 24 Z de
antes, nenhum ADC só na referência.

Mas no PS2 o `BondCarShadow` nunca é desenhado: `RVehicle::RenderShadowGeometry` (0x1d3d20) e `RenderShadowVolume`
(0x1d3d48) não têm quem os chame, e o `RShadowMap::Draw` (0x1ec2d8 → 0x1ec370) só faz a sombra simples (abaixo). O
modelo do 'as' do tipo 3 só decide se o carro tem sombra (`RVehicle` +0x350). A versão de Xbox projeta a sombra numa
textura; a do PS2, não.

### A sombra simples (`render/shadow.h`)

`RShadowMap::DrawSimpleShadow` (0x1ec4a0): um quadrado sob o carro. As quatro rodas (as alavancas do corpo no mundo mais a
posição, a altura de cada uma mais o `GetWheelRoadHeight`), o centro (x e z do corpo, y a média das rodas + 0,1), o nível
round((2 − min(2, a maior queda)) × 0,5 × ((força × 255) >> 8)), os dois eixos (as diferenças das rodas somadas aos pares;
desiste com |y| > 3 de lado ou 3,7 de frente) vezes a metade da largura e do comprimento do carro (a tabela por
`RVehicle` +0x18; 0,8 × 0,8 por padrão, as chaves "Shadow width/length{carro}" do banco "Render:FX"), a textura `cshd`
do `common.ssh`, a cor (nível, nível, nível, nível), ALPHA 0x62 com FIX 0x80 (Cd − Cs), sem gravar Z. A força é o "PS2
shadow strength": 24 (o `LoadAttributes` 0x1eb7f0 a põe; o .data diz 40). O nível fica em 21-23, então a sombra tira no
máximo 41/255 do chão sob o centro (a textura é um disco claro que se apaga para as bordas): uma sombra fraca, como no PS2.
Não desenhada: o dono não é um veículo, o chão sob o centro quase de pé (normal y < 0,1), uma roda 2 m acima do chão, o
carro tombado.

### O RVehicle (`render/vehicle.h`)

O objeto de render do carro com o leiaute do PS2 (0x370 bytes: o `RSceneObj`, o `RSkeletalObj`, o `RVehicle`):

- `RVehicle::PostLoad` (0x1d2648, com o `RSkeletalObj::PostLoad` 0x1cc6b0): o esqueleto do `Skel` do CARP (os nomes; o
  `GetBoneIndex` devolve a posição + 1), os ossos `leftfrontwheel`... e `leftfronthub`... (nos submarinos `leftfin`,
  `rightfin`, `leftrudder`, `rightrudder`), a altura de cada roda no modelo (a instância 0xfe do osso: y − (BaseDesc +0x38
  − a altura da caixa)), os modelos da sombra (o primeiro 'as' do tipo 3 e do 4). Quirk mantido: a busca da instância de
  cada roda continua de onde a da anterior parou. No Vanquish há 10 ossos: as rodas 3, 7, 4, 8 e só os cubos da frente
  (2, 6): as rodas de trás giram sem virar, presas a uma matriz da pilha;
- `ComputeWheels` (0x1d2cb0): o cubo de cada roda girado em y pela direção desenhada (o `GetCarSteer` limitado a
  ±0,075 volta e andando no máximo 0,01 por quadro, 0,002 na vista de dentro nas classes 0 e 1; a da direita ao
  contrário), com a altura da mola na linha 3 (raio + min(limite de desenho, compressão) − a altura da roda − o descanso),
  e a roda girada em x pelo `GetCarWheelSpinAngle` vezes o cubo; nos submarinos as barbatanas pelo controle vertical e
  os lemes pelo freio;
- `UpdatePosition` (0x1d34a8; o argumento 1 vem do `PhysicsObject::Simulate` 0x195aa8): o `RSceneObj::UpdatePosition`
  (0x1c8d70: a instância de origem na orientação e na posição do corpo menos o "cima" vezes o `GetRenderOffset`; andando
  1e-5 ou mais refaz o nó do mapa e o brilho de sombra, contados), as molas (contadas), `ComputeWheels`, o volante
  (o parâmetro 0 = direção × −750 graus) e, no carro do jogador (a Simulation +0xdcc), o velocímetro e o conta-giros
  (`GetNeedlePos` 0x1d3440 com a tabela 0x3340f0; km/h × 1,5 ou 0 andando para trás, e o `CalculateRPM`);
- o `Render` (0x1d35d8): a sombra, `ComputeDamage` (0x1d3138: (dano, escurecimento, 3 × escurecimento, o inteiro 127 se
  o dano passa de 0,75)), o `SetPerObjectBuffers` (0x1d2b38, pelo `RRenderSharedData::SendPerCarInstance`: os botões 9 e
  10 se apagando em 30 quadros, o w de cada lâmpada no `GAME::SwitchData` e as zonas no `GAME::DamageZones`), o
  `SetProcAnimState` e cada instância pela sua função do `gProcAnimFunc` (0x333a60): 0xff a matriz da instância, 0xfe o
  osso vezes a instância, 0xef a rotação em z pelo parâmetro (os ponteiros, `BuildRotate` 0x2d11c8), vezes a matriz do
  objeto. A matriz padrão do `SetProcAnimXFormList` sem lista (0x383430) é zero, não a identidade (nada no ELF a escreve;
  o nightfire-cxbx também a tem zerada): uma instância 0xfe sem esqueleto some. O carro do jogador não é desenhado na vista
  de dentro (o tipo 1 do modo da câmera, ou a transição para ele);
- as lâmpadas: o construtor deixa acesas a 7 (painel), a 8 (sempre acesas) e as 11-15 (Misc); o `PBondCar` liga e
  desliga as outras com `GlareOn`/`GlareOff` (0x18b328, 0x18b408: o `TriggerFX` 0/2 com 1 << (i + 16), que é um efeito,
  e o 0xb/0xc, que põe 1 ou 0 no +0x268): o farol (1) no construtor do carro do jogador (classe 0); no `Simulate`
  (0x188510-0x18871c) o freio (2) freando para a frente ou acelerando de ré a mais de 2,5 m/s, a ré (5) freando de ré, e
  as setas (`HandleTurnSignals` 0x18b580: o período sem sinal, o cancelamento pela direção); o x, y e z de cada entrada
  vêm do banco "Render:SwitchEffects"
  (`InitSharedBuffers` 0x1d2910: VScroll, Glow, Illuminate).

Fora (contado em `vehicle_pending`): o nó do mapa e o brilho de sombra (`WTree::FindNode`, `GetShadowBrightness`: o
`RLightManager`), as molas (`RSkeletalObj::UpdateSpringMassSystem` 0x1cc9b0: só rodam com as animações de estímulo do
`RAnimEngine` de uma peça batida), os efeitos (`TriggerEffects`, as partículas, as faíscas, o clarão do tiro, o dissolver
da cor, os relâmpagos do EMP) e as bolhas do submarino.

### A vibração (`game/feedback.h`)

O `IFeedback` (actionqueuemgr.obj): quatro portas (`IFeedbackPort`, 0x34 bytes no .data 0x32c468); o carro do jogador
ganha a sua no construtor do `PBondCar`. O `Update` (0x165bc8, uma vez por tick do temporizador; no port por passo da
simulação) liga o motor pequeno do DualShock por um pontilhado (a tabela 0x3738d0: o contador com os bits invertidos,
menos a última entrada, 254) contra o nível (a base × 100, o `SetVibrate`, o tranco que diminui) e o grande (0..255) pela
base × 125, o tranco e o `SetRumble`; com a vibração desligada (o .data 0x32c460, a opção do jogo a pé no bloco da troca
+0x994) ou em pausa, tudo parado. `ApplyJolt` (0x165fb0): forte (> 0,25) 255 por round(32 f) quadros (até 24), fraco
round(1020 f) por 8, só substituindo um tranco mais fraco ou mais curto; o `~ECollision` (0x149ed8) o chama pela força do
choque (`AddCollisionInfo` 0x1660a0, × 0,2 contra um objeto do tipo 0x0e). No `nf_driving` o `PAD_setactuator` vai ao
controle do PC como o jogo a pé faz (`platform/gamepad.h`: o motor grande no de baixa frequência, o pequeno ligado em
0xffff), só quando muda e só na janela; nas capturas é contado.

### O Master.def (`game/input_config.h`)

O `InputConfigManager::InitAndPreload` (0x1668a0): o `data\Control\Master.def` lista as configurações por tipo ("POV 8"
e os `povcfg0..7.def`, "Driving 1" e o `drivecfg.def` no MIS01.VIV); o `FrontEnd.def` e o `.def` de cada uma são
carregados, o `ParseDefFileForFrontEnd` lê de cada um os números de texto do menu do controle (a parte FRONTEND, pelos 14
nomes da tabela 0x32e540), a configuração de cada tipo vem do bloco da troca (+0xa14 o carro, +0x98c o POV, +0xa24 o eixo
invertido; fora da lista vira 0 com o aviso "Uberbond passed in invalid control config, setting to 0") e o `SetConfig`
monta a tabela do IOModule com o `FrontEnd.def` e o `.def` do carro (as ações do menu ficam na mesma tabela: na captura do
Vanquish 266 ações mandadas em vez de 256). `SetConfig(tipo, n)`, `GetNumConfigs` e `GetCurrentConfig` também. A
largada da missão (`GameLoop_StartUp`, as regras e os gatilhos do `SMissionManager`) fica para a fase 2.

### Os personagens no `--check` (a fase 0)

`nf_driving --check` agora carrega também os personagens de cada `.VIV`: os modelos (`data\actors\models\*.dat` com o
`.rel`: o `EAGL::DynamicLoader` partido, `CarModel::load_eagl`, os `__Model` desenhados pelo DMA e o VIF como as peças do
carro), as armas (`data\actors\weapons\*.crp`, como um carro; quatro, como o `sniper.crp`, só têm o objeto articulado e o
grupo `Shar`: o modelo é o de outra arma), os esqueletos (`data\actors\skeleton\*.o`, os `__Bone`) e os bancos de
animação (`data\actors\anims\*.dat`). Nos nove arquivos: 73 modelos, 14 armas (+4 sem modelo próprio), 72 esqueletos (1.736
ossos), 89 bancos de animação, nenhuma falha. Os modelos carregam mas não se desenham ainda: os métodos são de pele
(`BondLitTextureAndShadow_Skin` 100 lotes, `BondLitTextureAndShadowSpecular_Skin` 22, `BondGouraud` 3) e o carregador
não acha os símbolos que o jogo registra para eles (as mensagens mais comuns: `EAGLAnimationBuffer`,
`GAME::ActorLightSources`, `GAME::CharacterOptions`, `GAME::PositionalLights`, `GAME::CharacterLightingDullProps`,
`GAME::WeaponReflectionMap`...): é o que o desenho dos personagens (fase 2) precisa. Desenhados e animados desde a seção
20.

### A conferência (a parte 12 do `nf_driving_check`, `--so carro`)

O código original no interpretador MIPS (com a VU0 em modo macro) ao lado do porte, com um Vanquish do disco, o corpo, o
RVehicle e o esqueleto sorteados (`tests/driving_check_vehicle.cpp`; o carro com uma tabela virtual falsa cujas entradas
são as funções originais, a câmera do jogador e o `fHeliCamInfo` na RAM emulada). Com `--casos 4000`:

| O que | Casos | Diferentes |
|---|---|---|
| `ComputeWheels` (804 submarinos, 1.218 na vista de dentro) | 4.000 | 0 |
| `UpdatePosition` (2.772 do jogador, 3.176 andaram) | 4.000 | 0 |
| `ComputeDamage` | 2.000 | 0 |
| `SetPerObjectBuffers` e `ClearSharedBuffers` | 2.000 | 0 |
| `TriggerFX` (0xb, 0xc) e `TriggerIlluminate` | 2.000 | 0 |
| `GetNeedlePos` | 4.000 | 0 |
| `GetInstanceMatrix` (`SetProcAnimState`; 0xff, 0xfe, 0xef) | 16.000 | 0 |
| `BuildRotate` | 4.000 | 0 |
| `DrawSimpleShadow` (1.942 desenhadas; as outras por cada motivo) | 4.000 | 0 |
| `GlareOn`, `GlareOff`, `HandleTurnSignals` | 2.000 | 0 |
| a tabela do pontilhado do `IFeedback` | 256 | 0 |
| `IFeedback` (sequências de 60 operações: 30.000 operações, 38.436 `PAD_setactuator`) | 500 | 0 |
| `InputConfigManager` (`InitAndPreload` sobre o Master.def do disco, `SetConfig(tipo, n)`, os `Get`) | 12 | 0 |

O interpretador ganhou DIV, DIVU, PADDW (o "clear" de 128 bits do GCC), PEXEW e PEXCW. A conferência achou dois erros de
leitura (a matriz padrão zero, a última entrada do pontilhado) e pôs duas guardas (uma lâmpada fora de 0..15 no
`TriggerFX`/`TriggerIlluminate`). Divergência: o `BuildRotate` usa `nf::fm::sin/cos` no lugar do sinf/cosf do newlib (a
parte 12 troca os do interpretador pelos mesmos, como a 6).

### No `nf_driving`

`--drive` desenha o carro como o `RVehicle::Render`: o `UpdatePosition` (com o modo da câmera do jogador), a sombra antes
das peças, o `ComputeDamage`, o `SetPerObjectBuffers` (as lâmpadas de cada lote `MatCarTextureSwitchPacked` lidas do
`SwitchData` do quadro) e as instâncias pelo `GetInstanceMatrix`; a vibração e o `InputConfigManager` como acima. Na curva
de Paris (`--drive-input "none:1 W:2 W+A:1.2"`) as rodas giram 0,14 volta, a direção desenhada fica em −0,01, o farol e as
lâmpadas fixas acesas (1, 7, 8, 11-15) e a sombra no nível 21. `--ground` (só do port) põe o carro sozinho assentado num
chão plano pela física (2 s de passos), com as rodas, as lâmpadas e a sombra, o fundo fazendo de chão. O quadro inteiro
(`--bench-frames 600` a 1080p): 4,77 ms na Radeon integrada (4,51 sem esta etapa), 0,77 ms na RTX 5090 (0,63); o render do
carro na CPU custa 0,15 ms por quadro (as instâncias pela aritmética da VU0 do PS2, `carro_cpu_ms` na linha da medida).

### O que falta da fase 1 e a estimativa

Da lista da seção 14, o carro, a entrada e a vibração e a fase 0 estão feitos; a visibilidade do mundo e os métodos do
mundo são da seção 15. O que sobra do carro passa para a fase 2, com o que o pede: as molas (com as animações de estímulo
do `RAnimEngine`), os efeitos do `TriggerFX` (partículas, faíscas, marcas de pneu, o EMP), o nó do mapa e o brilho de
sombra (`RLightManager`), o desenho dos personagens (os métodos de pele, o `EAGLAnimationBuffer`, as luzes dos atores) e a
largada da missão. Com as seções 15 e 16 juntas na main, o que resta da fase 1 é o resto do `RSceneObj` (seção 15: 1-2 dias; os métodos
das outras pistas vêm com as missões delas); fases 2-4: 16-27 dias (eram 15-25; +1-2 pelos personagens e os efeitos que
passaram para lá).

## 17. A fase 2: o som (o SND da EA, o SNDDRV.IRX e o som do carro)

O som das missões de carro com o código do jogo, do motor do Vanquish ao alto-falante: o SND 7.42.0 da EA no EE (o
`libsndps2z`, "SNDAUTHOR Blair Bitonti"), o decodificador de EA-XA, o `SNDDRV.IRX` no IOP e, por cima, o
`BondAudioPS2` do carro do jogador, sobre uma SPU2 do port (a `platform/spu2.h` do jogo a pé, numa instância só do
carro). Tudo portado do MIPS do PS2 (o `DRIVING.ELF` e o módulo do IOP) e conferido contra o código original no
interpretador (a parte 13 do `nf_driving_check`, `--so som`) ou, no IOP, pelo que se ouve (`tests/test_driving_sound.cpp`).
O nightfire-cxbx (NightfireResearch: o mesmo SND na versão de Xbox, `src/driving/sound/snd/`) ajudou a ler as camadas de
cima; os números são todos do PS2.

### O caminho de um som

O jogo chama a API do SND (`SNDBANK_play`, `SNDvol`, `SND3dpos`, `SNDpitchmult`, `SNDstop`, `SNDfxinitbus`...); o gerente
das vozes lógicas (`SNDVOICEI_*`, 0x88 bytes cada, 62 vozes: 48 da SPU2, 6 do mixador do IOP, 8 do mixador do EE) aloca
com roubo por prioridade e o servidor de 100 Hz anda os envelopes, os LFOs e os desvanecimentos; o modo de render de cada
timbre escolhe o lugar da voz (8 a SPU2, 0x100 o mixador do IOP, 4 o do EE; os modos do jogo são 0x28, 0x24 e 0x120). O
estado de cada voz da SPU2 vai ao IOP numa "tagbuf" (0x2c00 bytes em 0x35b380, escrita pelo endereço sem cache
0x2035b380) a cada tick do driver: 512 amostras de 48 kHz, 93,75 por segundo, a metade do buffer em laço da saída. O
`SNDDRV.IRX` a percorre (`SNDIOP_processtagbuf`) e manda à SPU2 só o que mudou: o key on com o SSA, o LSAX no laço
silencioso de 0x5000 (o laço das amostras vem das flags do PS-ADPCM), o ADSR 0x000f/0x0005, os volumes pela rampa da
SPU2 quando o salto é grande (`volramplut`), os bits de seco e de molhado; e devolve ao EE o endereço atual de cada voz
(0 quando ela caiu no laço silencioso: o EE a libera).

Os bancos são BNKl versão 5 com um cabeçalho PT por timbre (`SNDI_gettag`, `SNDI_parsetimbre`): o `SNDbankadd` manda a
parte da SPU2 pela fila de transferências do IOP (`SNDIOP_dmqueue`, `sceSdVoiceTrans`) e a do IOP para a memória dele
(o `sceSifAllocIopHeap`, aqui o modelo `IopHeapModel`), e troca o dado da etiqueta 0x8a de cada timbre pelo endereço da
amostra na SPU2. Nos 56 bancos do disco há 1.468 timbres: 1.219 de PS-ADPCM na SPU2 e 249 de EA-XA no mixador do IOP (os
bancos `h*` dos personagens: os gritos, quedas e golpes dos capangas e do Bond; os `a*` do ambiente e 3 de cada `g*`).

O reverb: o `AFX` do jogo pede um modo por ambiente (na Paris "X:City", o índice 20 da tabela 0x355f08: o modo 50 do
`SNDfxinitbus`), o `SNDDRV_setspufx` o traduz no tipo da SPU2 (50 → 6, o "Space" do libsd) e reserva a área no topo da RAM
(0x200000 − 0x2f6c0), e o IOP (`SNDIOP_checkspueffect`) baixa o volume do efeito, troca o tipo, põe o EEA do núcleo 1 em
0x1c0000 e zera a área de trabalho em pedaços de 0x1000 bytes por tick, do topo (0x1e0000) até o ESA. O registro EEA só
guarda os bits 17-20 do endereço: a área acaba em 0x1dffff e o ESA do tipo 6 é 0x1d0940, exatamente o que o EE reservou.
O port contava o ESA a partir do EEA pedido (0x1b0941), e o IOP zerava 128 KB a mais, apagando as flags de fim do banco
do motor: o som sumia entre 8,0 e 8,25 s da volta de teste.

### O EA-XA (`eaxa.h`)

O ADPCM da EA dos fluxos (`.asf` dentro dos `.MUS` e `.SPE`: música e falas, 48 kHz): blocos de 15 bytes por canal, 28
amostras, o filtro e o deslocamento no byte 0, cada amostra montada no acumulador da FPU do EE (`ADDA.S`, `MADDA.S`,
`MADD.S`, cada passo truncado). `SND::CEAXABLKDecf` (o construtor 0x318d90, `Feed` 0x318db0, `Decode` 0x318dd8,
`SetState` 0x319018) e `SND::decodexac` (0x318900). Os 236 fluxos do disco têm 17.705.434 blocos, nenhum com um filtro
fora da tabela (a guarda: no PS2 um filtro de 4 a 15 leria o .data depois dela). Os fluxos ainda não tocam (o `AStream` e
o mixador do EE não estão portados), mas o decodificador está pronto e conferido.

### O núcleo do SND (`snd.h`, `snd_core.cpp`, `snd_ps2.cpp`)

65 funções do SND (o sistema `SNDSYSI_init`, o heap `SNDMEMI`, o servidor de 100 Hz, o gerente das vozes, os bancos, as
etiquetas, a API, os envelopes e LFOs, o volume e o pitch em ponto fixo, o random de seis palavras `iSNDrandom`) e 39 da
plataforma do PS2 (e duas tabelas; `SNDPLATFORM_*`, `SNDDRV_*`: as opções, a iniciação do IOP pelo RPC 0, o alocador da RAM da SPU2, a
tagbuf, o reverb da SPU2 e o do mixador do IOP pelo RPC 2, a fila de transferências), com os leiautes do PS2 (o estado em
`snd_state.h`: os endereços de cada global). Divergências (nos cabeçalhos): o tick do driver na thread do jogo (o PS2 tem
uma thread do EE acordada pelo IOP), os mutexes vazios (o contador da seção crítica continua contando), o SIF como
chamadas diretas ao IOP portado, as transferências que completam na hora, só o caminho sem DTS; e três guardas: um 13º
timbre num som (o original escreve por cima da pilha), uma voz "−1" no `SNDVOICEI_free` e um barramento de efeito
diferente de 0 (o original lê além da tabela).

### O IOP (`snddrv_iop.h`)

22 funções do `SNDDRV.IRX` (os nomes da `.symtab`; marcadores `// [snddrv] 0x<endereço>`): a fila de transferências
(`SNDIOP_dmqueue`, `dmservice`, `dmtransfer`, `dmcallback`, `dmcomplete`), as vozes da SPU2 (`SNDIOP_updatevoices`,
`keyon`, `keyoff`, `spusetvol`, os bits de seco e molhado), o reverb (`SNDIOP_checkspueffect`), a mistura do EE na saída
(`SNDIOP_mix`, `SNDIOP_ee48_iop48`), `SNDIOP_processtagbuf`, `SNDIOP_init`, o RPC e o comando de transferência. As duas
threads do IOP viram uma volta (`snddrv_iop_tick`) chamada pelo tick do EE. Fora: o mixador do IOP (os 249 timbres de
EA-XA; cada tick com uma voz do IOP pedida conta uma guarda) e o DTS. O módulo não roda no interpretador (seria preciso
relocar o IRX e simular o kernel do IOP): a parte 13 confere o que chega a ele do EE original (o bloco de iniciação, as
3.773 transferências dos bancos da Paris e a memória da SPU2 e do IOP depois de cada banco, byte a byte) e os testes o
que sai dele (abaixo).

### A SPU2 e a saída (`snd_spu.h`, `platform/audio_out.h`)

Uma `nf::Spu2` só do carro atrás das funções do `LIBSD.IRX` que o driver chama (`SndLibsd`: `SetParam`, `SetSwitch`,
`SetAddr`, `GetAddr`, `SetEffectAttr`, `VoiceTrans`, `BlockTrans`). `snd_render` anda as amostras e, a cada 512, roda o
IOP e o EE antes de a SPU2 tocar o pedaço; o que o IOP escreve na metade livre do buffer (o mixador do EE e o do IOP)
entra na entrada de dados do núcleo 0 com um tick de atraso, como no PS2. A saída vai pela `platform/audio_out.h` do jogo
a pé com uma fonte trocada (`audio_out_set_source`, nula = a SPU2 do jogo a pé: o jogo a pé não muda, o WAV de 07000012 é
o mesmo byte a byte).

### O som do jogo (`audio.h`)

O `BondAudioPS2` (56 funções: 51 dele e do `SupportPS2`, e as 5 do jogo que o alimentam, o
`PBondCar::InitAudioObject`, o fim do `PBondCar::Simulate`, a parte de som do `ECollision`, o `ESimFrameUpdate` e o
`EAudioUpdate`): `ASystem` (as opções do SND para o modo de saída da troca, o heap de 0x10000, o volume
mestre 0x40), a parte de efeitos do `ASoundManager::Init` (os volumes do `data/audio/<pista>.ini` e os mestres fixos, o
`AFX`, o `ASystem`, os bancos das dez categorias do `banks.ini` com os nomes dos `.h`: "SFX_" e o nome), `AMix` (o
volume, o mestre, a transição, as somas do quadro), `ABank`, `AIndex`, `AFX` (o reverb que desce a zero, troca de modo e
sobe ao volume da mixagem do modo), `AVoice` com as suas três vistas e o `PlayVoices` (o que vai ao SND a cada quadro:
o atraso em ticks do TIMER, o `SNDBANK_play`, ou volume, pitch, azimute e efeito, ou o fim), `ABaseSound` e a lista do
`ASoundManager`, `AOneShotSound`, `APath::Build` (a distância, a queda de volume (máx − d) / ((máx − mín) + 8 (d − mín)),
a faixa de altura do ambiente, o azimute pelo atan2 no referencial do ouvinte, o Doppler 400 / (400 + vs − vl), o atraso
(d − 25) / 400), o `AListener` na câmera (o `EAudioUpdate`) e o `BuildPaths`. O carro do jogador: `APlayerVehicle` sobre
`AVehicle` (os raspões de rocha, metal e areia, as derrapagens por superfície, o ruído do chão, os solavancos dos
paralelepípedos e dos trilhos a cada 150 e 250, o vento, o ruído do asfalto acima de 15 m/s trocado pela superfície,
`PlayLanding`, `PlayCollision` com os 12 timbres por força e os descansos de 5 ticks), `ARaceEngine`/`AEngine` (dez
timbres: dentro e fora do carro cruzados entre 1 e 2 m da câmera; idle, carga baixa, média e alta e "cruz" pelas rotações
suavizadas (o alvo: as do carro mais a oscilação de ±150, menos a derrapagem de trás × 1000; com uma roda de trás no ar,
10.000 acelerando) e pela carga; o pitch (rotações + 2000) / 4500 e o "vaccuum" quando a carga passa de 0,2) e
`AVehicleWind`. O fim do `PBondCar::Simulate` (0x188c64..0x189390) alimenta o objeto de som a cada passo, e o
`ECollision` (os choques, o chão e as aterrissagens: o port agora entrega os três a `collide_events().collision`, como o
PS2, o que também leva o chão e as aterrissagens à vibração) chama o `PlayCollision` e o `PlayLanding`. O `TIMER` de 100
Hz anda com o relógio do jogo.

A conferência achou dois erros de leitura do port: o `random()` do jogo devolve a palavra estendida SEM sinal para 64
bits (`dsll32`/`dsrl32`), então o `bltz` do `rrandom` nunca salta e o `cvt.s.w` lê a palavra com sinal: o `rrandom` cai
em [−0,5, 0,5), não em [0, 1) (o nível e o pitch do vento passeiam diferente; mantido); e o `operator new` do
`ABaseSound` (0x2fca38) põe o som novo no começo da lista (insere antes do `begin()`), não no fim. Mantido também: uma
vista em laço cujo som o SND acabou (roubado por prioridade) é parada e tocada de novo no quadro seguinte.

Fora (contado ou anotado em cada lugar): as falas e a música (o `AStream`, o mixador do EE, a música interativa), o
mixador do IOP (o ambiente da Paris, os gritos dos personagens, o `SFX_OffRN` e o `SFX_OffRoadSkid` do carro fora do
asfalto), o `AFader`, as armas do carro (`AGun`, `ATurret`, `AZoomObj`: o PS2 cria o "Machine Gun" do jogador, mudo sem
tiro), a buzina e o alarme, os outros veículos (`ATrafficVehicle`, o submarino, os carros da IA), os objetos do mundo
(`WSoundGroup`, `AWorldSound`, `ASmackable`), os personagens e os sons dos menus.

### A conferência (a parte 13 do `nf_driving_check`, `--so som`)

O código original no interpretador MIPS (com a VU0 em modo macro) ao lado do porte (`tests/driving_check_sound.cpp`). O
interpretador ganhou o espelho sem cache da RAM, MTLO/MTHI, as variantes de multiplicação e divisão do segundo pipeline
(MADD/MADDU/MADD1/MADDU1, MULT1/MULTU1/DIV1/DIVU1, MFLO1/MFHI1/MTLO1/MTHI1), DSLLV/DSRLV/DSRAV e até 8 argumentos.
Com `--casos 4000`:

| O que | Casos | Diferentes |
|---|---|---|
| EA-XA: pacotes de verdade dos 236 fluxos em pedidos de tamanho sorteado (as amostras em float, o retorno, o estado) | 3.126 pacotes (20.269 `Decode`, 16.742 com um bloco pela metade; 9.991.563 amostras) | 0 |
| SND: a iniciação (o estado inteiro, o bloco do IOP) | 1 | 0 |
| SND: os 56 bancos dos 9 arquivos, um por vez (3.765 entradas de timbre: as etiquetas, o alocador, as transferências, a RAM da SPU2 e do IOP, `SNDbankpatpresent`, `SNDbankremove`) | 56 | 0 |
| SND: a API sorteada sobre os 9 bancos da Paris, com os ticks do driver e as vozes que o IOP libera (o estado inteiro do SND depois de cada operação; 3.773 transferências) | 16.000 operações (4.751 `SNDBANK_play`, 2.284 tocaram; 3.175 ticks do driver, 1.003 vozes liberadas pelo IOP) | 0 |
| `rrandom` (estados sorteados) | 8.000 sorteios (4.048 negativos) | 0 |
| `AVoice::Play` (com o `View::Play/Stop/Push` e a lista ativa) e `AVoice::Set` | 8.000 (1.326 entraram na lista ativa, 1.288 saíram) | 0 |
| `APath::Build` (ouvintes e sons sorteados, a faixa de altura, as distâncias limite) | 4.000 | 0 |
| o carro do jogador: `APlayerVehicle::Play` (com o `AVehicle`, o `ARaceEngine`, o `AEngine`, o `AVehicleWind`), `PlayCollision`, `PlayLanding`, quadro a quadro com os bancos, os volumes e os índices da Paris (tudo: os campos, as vozes, a lista ativa, as mixagens, o random, os descansos, os avulsos criados) | 7.339 quadros, 416 choques e 245 aterrissagens (1.365 avulsos criados, 2.511 trocas do ruído do asfalto) | 0 |

Ganchos nativos: o `FastAlloc`/`new` (um alocador linear), o `strcpy`, o `AIndex::Lookup` (um mapa da STL: o do porte
nos dois lados), o `AMix::Add`/`Get` (a mixagem do mesmo nome) e o `atan2f` do newlib (o `nf::fm::atan2`, como no porte:
a divergência anotada no `APath::Build`). O 12º descanso dos choques fica no máximo no tick atual antes de cada
`PlayCollision` (o original não para no fim da tabela dos descansos e lê o que vem depois na memória; o porte para).

Os testes (`tests/test_driving_sound.cpp`, sem dados do jogo): o EA-XA (os nibbles, o filtro 1 decaindo por 0,9375), um
banco feito à mão (uma onda quadrada de PS-ADPCM, 24 kHz) tocando pela cadeia inteira: 1.714 cruzamentos por segundo, o
dobro com o `SNDpitchmult(0x2000)`, o silêncio depois do `SNDstop` e uma amostra sem laço que acaba sozinha (o IOP vê o
endereço no laço silencioso e o EE libera a voz); a área do reverb (o ESA do tipo 6 em 0x1d0940); o heap do IOP; a
transição e o mestre do `AMix`; o `AVoice` (o volume em 0..127, o pitch 4.12 limitado a 4, o azimute, a lista ativa); a
geometria do `APath` (à frente, à direita, o Doppler, além do máximo); o pitch do motor seguindo as rotações.

### No `nf_driving`

`--drive` liga o som como o jogo: o `ASoundManager::Init` com os arquivos do `.VIV` da missão (9 bancos na Paris), o
`PBondCar::InitAudioObject` (as chaves `SFX_ENGINE`, `SFX_VOLUME`, `SFX_SKIDMULTIPLE` do carro), o `ESimFrameUpdate`, o
`EAudioUpdate` (o ouvinte na câmera do jogador) e o `ECollision`. Na janela o som vai ao aparelho (XAudio2 no Windows; `--no-audio`
o desliga); nas capturas só com `--audio-dump <wav>` (senão nada é misturado). O relógio do áudio é o do jogo: 800
amostras por passo de 1/60 s, o `TIMER` de 100 Hz pelo mesmo relógio. `NF_DRV_SND_LOG=<arquivo>` registra cada chamada do
`PlayVoices` ao SND (o tick, o banco e o timbre, o volume, o pitch, o azimute, o efeito). O relatório da captura ganha a
linha `som` (bancos, sons, vozes ativas, tocou, paradas, avulsos, choques, aterrissagens, rotações, pitch e carga do motor,
o reverb, as guardas, o pico e o RMS da saída, os ticks do driver).

A volta de teste (`--car-pos -327.7,20,-17.7,0.3155 --drive-input "none:3 W:5 none:3"`, 11 s;
`python tools/sound.py analyse <wav> --window 1`): sem cortes, pico −7,4 dB (o choque no muro), RMS de −30 dB em marcha
lenta; os cruzamentos por zero sobem de 340-420 Hz parado a 785 Hz no primeiro segundo de aceleração e 1.226-1.258 Hz a
toda, ficam em ~1.200 Hz encostado no muro com o acelerador e voltam a 350 Hz soltando; pelo registro do SND, o pitch das
vozes do motor sobe de 0,55-0,60 (marcha lenta) a 1,47-1,54 acelerando e volta a 0,6. A queda do carro na rua no começo
toca os pneus (`SFX_TiresHeavy`) e o `SFX_Cobble1` (0,1 s), o muro o `SFX_Light3` (5,48 s), o `SFX_Light1` 0,15 s depois
e o raspão de rocha (`SFX_ScrapeRock`). Custo: o quadro inteiro
(`--bench-frames 600` a 1080p na Radeon integrada) de 6,63 ms sem som a 6,93-6,99 ms com o som e o WAV (+0,3 ms).

O que só o PCSX2 (ou o hardware) confirma: a interpolação da SPU2 (gaussiana no PS2, cúbica na `nf::Spu2`) e o reverb a
24 kHz (as notas da `platform/spu2.h`); o nível absoluto da saída; o ritmo do `TIMER` de 100 Hz (a interrupção do contador
1) contra o tick do driver (93,75 por segundo) e o relógio do jogo; e a sobreposição da área do reverb com os bancos: os
bancos entram antes de o `AFX` pedir o reverb, então um banco que passasse de 0x1d0940 seria zerado pelo IOP também no
PS2 (na Paris eles acabam em 0x1bac40).

### O que falta da fase 2 e a estimativa

Do som: o mixador do IOP (o SFILTER e o MIX do módulo: os 249 timbres de EA-XA), o mixador do EE e o `AStream` (a música
dos `.MUS` com a música interativa, as falas dos `.SPE`), os outros objetos de som (o tráfego, as armas, o mundo, os
personagens, o `AFader`) e, como conferência a mais, o `SNDDRV.IRX` no interpretador (relocar o módulo, os imports do
kernel do IOP): 3-5 dias, junto com o que os pede (o tráfego, as armas e os personagens da fase 2, as falas da fase 3).
A pilha do SND, o EA-XA, o IOP e o carro do jogador estão feitos: as fases 2-4 ficam em 13-23 dias (eram 16-27).

## 18. A fase 2: o mundo vivo (o tráfego, os gatilhos, os objetos que o carro derruba)

Paris com o tráfego andando nas ruas e o mobiliário urbano que o carro derruba, sem o som e sem os personagens (a seção
17 é a do som). Tudo do MIPS do PS2 e conferido contra o código original no interpretador (a parte 14 do
`nf_driving_check`, `--so trafego`); o nightfire-cxbx (NightfireResearch) deu os nomes e a estrutura onde tinha (o
`RoadNetwork`, o `RoadNav`, o `Trigger`, o `TriggerManager` da versão de Xbox; o `AIGroundVehicle` e o `Smackable` eles não
reconstruíram: os nomes vêm do `DRIVING.SYM` e do uso).

### A rede de estradas e o navegador (`ai/road_network.h`, `ai/road_nav.h`, `ai/sim_random.h`)

O grupo `RNgp` do CARP (o `RNhd` com as contagens; `rn` os nós, `rs` os trechos, `ri` os cruzamentos, `rj`, `rr` as ruas)
montado como o `WRoadNetwork::Init` (as tabelas de ponteiros, os carimbos de 8 bytes dos trechos, as contagens das ruas)
e as consultas: as pontas e os vetores de través, o comprimento, as larguras, o tipo e o índice da faixa, os pesos das
faixas, o nó oposto, os pontos (nos trechos curvos pela Bézier do `RCameraSpline` do `GetSegmentCurveStep`), o ponto mais
perto de uma reta. Paris: 880 nós, 811 trechos, 130 cruzamentos, 165 ruas. O `WRoadNav` (0xc0 bytes: um lugar num trecho,
o sentido, o t, a faixa e a curva que ele segue) com `InitAtPoint` (o `FindClosestSegmentInd` pela grade da colisão),
`InitAtSegment`, `IncNavPosition` no modo 1 (o do tráfego: `CalcNextSegmentLane`, o próximo trecho pelo `SimRandom`),
`ChangeLanes`, `ReverseNavDirection` e `UpdateLaneChange`; os modos 2-5 (calçada, direção, ao acaso) contam como
pendentes. O `SimRandom` (a primeira palavra da `Simulation`) e o `RRandom` do render (0x333e60) são o mesmo gerador de
semente de 16 bits; o `Simulation::Reset` reinicia o primeiro, o `Render_InitLibRender` o segundo (`RRandom::StartUp`).

### O tráfego (`ai/road_spawn.h`, `ai/ground_vehicle.h`, o `PBondCar` de classe 3)

O `GameLoop_StartUp` monta o tráfego depois do carro do jogador: o `AIVehicleController::CreateSimTrafficCars` sorteia os
tipos (`NUM_TRAFFIC_TYPES_PS2` da coleção `world` da pista: 3 em Paris, de sete: hatchback, sedan2, suv1 com o
`SimRandom` reiniciado) e uma cor para cada um dos `MAX_TRAFFIC` (20) carros, cada um um `PBondCar` de classe 3 montado na
origem, e o `RegisterSimCarList` dá a cada um o seu `AIGroundVehicle` (0x2a0 bytes, o vetor estático 0x3282d0) e o manda
para longe (`UnSpawn`: o corpo congelado, estado 3, em z = 40000 + 50 k). A cada quadro da IA (`EAIUpdate`) o
`UpdateVehicles` passa um em cada 8 pelo `AIGroundVehicle::Update` no estado do tráfego: `DoTrafficStateUpdate` (nascer,
sumir), `DoAttackMode` e `DoMoveMode` (os modos 1/12 e de navegação 1/3), o navegador à frente (`GetLookAhead`,
`UpdateNavPos`), os outros carros (`CheckTrafficCollision`, `TestForCollision`, `GetCollisionStopPoint`), os sinais dos
cruzamentos (`IntersectionGreenLight`: um ciclo a cada 300 passos, os últimos 150 todos vermelhos) e os controles do carro
(`DriveToPointTraffic`). O carro segue pelo `PBondCar::Simulate` de classe 3 (`ProcessSimplePhysics`,
`AddSimpleWheelForces`: um passo em dois longe da câmera). O `AIRoadSpawn` guarda os trechos em volta do jogador (até 50,
refeitos a cada 89 passos) e sorteia um lugar livre onde um carro pode nascer longe da vista.

Quem põe o tráfego na rua em Paris não é o `AIRoadSpawn`: a missão deixa o `fMaxActiveTraffic` em 0 (nenhum
`EChangeMaxTraffic`, o evento 120, nos gatilhos nem nas regras), e os carros nascem pelos gatilhos: 12 `ESpawnTraffic`
(evento 28: o primeiro carro congelado vai para o gatilho, de frente para ele, com a velocidade do evento), que o gatilho
703 (uma esfera na rua, -6, 787, tocada pelo carro do jogador) dispara pelos seus 18 `EFireRandomTrigger` (evento 203:
cada um sorteia pelo `RRandom` um de até quatro gatilhos). Fora da vista, com `fMaxActiveTraffic` 0, o
`DoTrafficStateUpdate` os guarda de novo.

### Os gatilhos (`game/triggers.h`)

O `WTriggerManager` do `WWorld::Open`: os 754 gatilhos do `Trgr` do `Map ` de Paris (616 caixas, 39 esferas, 99
cilindros; 0x40 bytes: o centro, o raio, a forma, as bandeiras, a altura, a lista de eventos `el`, a frente e o lado da
caixa), o `Restart` (a cópia de volta, os `EStartMission`), e a cada `ESimFrameUpdate` o `Update`: cada corpo rígido
acordado passa pelo `Process` (a grade em volta do corpo, a lista fixa de gatilhos de cada célula, o carimbo de consulta,
as bandeiras de quem toca: 4 os carros do jogador, 8 os de tipo 2, 0x40 os outros, o tráfego) e o `CheckCollide` (a
esfera pela distância, o cilindro pela altura, a caixa pelo `OBB::CheckOBBOverlap` das duas caixas; o sentido pelo
`TestDirection`). Um toque dispara a lista (`FireEvents`: o `EventDynamicData` de 0x64 bytes e a função criadora de cada
evento pela tabela 0x36fda0); os eventos rodam no fim da passagem do `EventManager`. O port executa o `ESpawnTraffic` (28),
o `EFireRandomTrigger` (203), o `EFireTriggerSpeedCondition` (171, 172) e o `EWakeupSmackable` (35, abaixo), com as
funções de cada um no `RegisterEvent::ResolveEvent` do `el` (as `TagReference` dos gatilhos e objetos); os outros 70 e
tantos números (os sons, os efeitos, a missão) contam por número e são descartados. Fora: os corpos simples, as
instâncias dos trilhos (`RPathEngine`), os raios das armas (`RayShell`), as listas dinâmicas da grade e as regras da
missão (o `Rule` do `SMissionManager`: 152 listas de eventos que nenhum gatilho usa, 13 delas `EFireRandomTrigger`).

### Os objetos que o carro derruba (`game/smackable.h`)

Cada objeto de cena derrubável de Paris (544: os postes, as placas, as bancas de jornal, as cadeiras, as lixeiras; o
registro do `ps  ` da instância com o bit 1 do byte +5) tem um gatilho com um `EWakeupSmackable` (os três registros dos
dados: o objeto, a instância, o `ps  `). Quando um carro o toca, o evento acorda o objeto: o
`Simulation::SpawnSmackableObject` (o `CanSpawnRigidBody`: com mais de 36 corpos, o derrubado dormindo mais longe vai
dormir de vez) cria o `Smackable` (0x88 bytes): um `PhysicsObject` com um corpo rígido de tipo 4 com a caixa do `pD` do
objeto (`GetBoundingDimensions`), as alavancas dos pontos de colisão do `pD` (`GetCollisionGeometry`), o tensor do
`Util_GenerateTensor` (uma barra quando 5 (x + z) < y: os postes) e a massa e a vida da `PhysicsData` da coleção
`smackable` do objeto (o símbolo `PHYS::Paris_StreetLight` do CARP: o `Track::load` registra o espaço `PHYS`, o
`PhysicsNamespace` do PS2; peculiaridade mantida: o `MOMENT` é registrado no mesmo +0xc do `FORCE_DETACH`), nascido parado
e dormindo. O choque com o carro na `SimulateGame` (`CollideWithObject`) o acorda e o empurra; o `Smackable::Simulate`
leva a instância para a pose do corpo (`RSceneObj::UpdatePosition`: as linhas da matriz da instância e, se andou, o nó
da árvore do mapa pelo `WTree::CheckContainment`/`FindNode`); o corte usa a posição do corpo
(`GetNextSceneObjCullInfo`). Parado por 0,1 s (`sleep_delay`, do `EWakeupSmackable`) ele dorme (`GoToSleep`: o gatilho vai
para onde o objeto ficou, `WTrigger::UpdateRotPos`, e volta a valer; o dano vai ao byte +4 do `ps  `), sai do mapa da
`Simulation` (`DeletePhysicsObject`) e é apagado no `DeleteDeadObjects` do próximo `ESimFrameUpdate` (o destrutor: o
`RSceneObj::SetPhysics(0)` deixa a instância na última pose do corpo, `VU0_SQTquattom4`; a vaga do corpo volta). O
`ApplyDamage` tira a vida (`LoseHitPoints`) e empurra no tiro (o tipo 2 com dano até 10: `ResolveForce`). Fora (contados):
o som (`ASmackable`), as animações de estímulo do `RAnimEngine` e o `TriggerFX` do render que o `ApplyDamage` pede (os
pedaços que se soltam), a luz de sombra do `UpdatePosition`, o objeto jogado (o ramo `Newton` do `EWakeupSmackable`) e a
lista dinâmica da grade (o gatilho movido pelo `GoToSleep` só é achado nas células da posição antiga: um objeto que caiu
longe dela não acorda de novo; a divergência mais visível).

### A conferência (a parte 14 do `nf_driving_check`, `--so trafego`)

O código original no interpretador MIPS com a VU0 contra o porte (`nf_driving_check . --casos 4000 --quadros 600`, tudo
com 0 diferentes):

- a) a rede: o `WRoadNetwork::Init` de Paris e 4.000 casos de cada consulta (524 nos trechos curvos);
- b) o navegador: 400 `InitAtPoint`, 6.651 passos de `IncNavPosition` (1.942 trocas de trecho), 678 `ChangeLanes`, 334
  `ReverseNavDirection`, 367 `UpdateLaneChange`, 320 `InitAtSegment`;
- c-e) a frota (o Vanquish e cinco carros do tráfego montados pelo construtor do porte, copiados para a RAM emulada a
  cada caso, com a tabela virtual do `PBondCar` original): 1.000 casos de cada consulta do `AIRoadSpawn`, do
  `IntersectionGreenLight`, do `TestForCollision` e do `GetCollisionStopPoint`, 1.000 `ProcessSimplePhysics` e
  `ResetCar`, 2.895 passos do `AIGroundVehicle::Update` (65 nascimentos, 123 sumiços, 38 sinais vermelhos);
- f) os gatilhos (o `Trgr` e as 754 listas `el` copiados): o `Init` com o `Restart`, 2.040 `CheckCollide`, 444 `Process`
  (com o `FireEvents`: o `LookupEvent` trocado por um gancho que anota o número, o gatilho, os dados e o
  `EventDynamicData`), 210 `Update`, 91 `Restart`, e os destrutores de 1.000 `EFireRandomTrigger` (com o `RRandom`), 1.000
  `EFireTriggerSpeedCondition` e 1.000 `ESpawnTraffic` (com os motoristas no vetor estático do PS2);
- g) os objetos derrubáveis (o objeto de cena, a instância, o `ps  `, o `pD` e a `PhysicsData` copiados; o mapa da
  `Simulation` e o som em ganchos; o nó da árvore do `UpdatePosition` fora, com o bit 26 ligado dos dois lados): 1.000
  `Util_GenerateTensor`, 1.000 `CheckContainment` com nós do `Wmap`, 1.000 `UpdateRotPos`, 597 `SpawnSmackableObject`,
  403 `EWakeupSmackable`, 3.030 passos do `Simulate` (566 `GoToSleep`), 791 `ApplyDamage` e 1.000 destrutores.

O `+0xc` do `RSceneObj` (o `pD` que o `UseArticle`/`UseInstanceList` grava) o port não grava: `scene_object_desc` o acha
quando precisa, e a parte g copia o achado. `tests/test_driving_traffic.cpp` traz as propriedades independentes (o toque
de cada forma de gatilho e o sentido, o tensor de uma caixa e de uma barra, o nó que contém um círculo, o gatilho que o
objeto leva).

### No `nf_driving`

`--drive` monta tudo como o `GameLoop_StartUp` (o `SimRandom` e o `RRandom` reiniciados, os gatilhos, o
`AIVehicleController`, a rede, o carro do jogador, o `AIRoadSpawn`, o tráfego) e roda os gatilhos no `ESimFrameUpdate`, o
tráfego no `EAIUpdate` e os eventos no fim de cada passagem do escalonador. Os carros do tráfego na rua (os de corpo não
congelado a menos de 400 m do olho; o `RRenderHigh` os põe na cena pela visibilidade, não portada) são desenhados com o
seu `RVehicle` como o do jogador (todas as instâncias: o `+0x348`, a lista de vista, só o carro do jogador recebe; o dano e
as luzes de cada carro numa constante por item); a cor sorteada não é desenhada. As instâncias que os objetos derrubados
movem refazem a matriz em cache do desenho (`Track::refresh_instance`). `--fire-trigger N` (depuração) dispara um gatilho
na largada (`703` põe o tráfego na rua do lado de -140, 600); `NF_DRV_TRIGGERS=1` lista os gatilhos, `NF_DRV_SMACK_LOG=1`
os objetos derrubados a cada passo; `--no-traffic` desliga o tráfego e os gatilhos. A linha `trafego` do relatório: os
carros, os tipos, os na rua e desenhados, os gatilhos e os eventos, o motorista; `objetos_derrubaveis`: os acordados,
os que dormiram e os vivos com a posição, a massa, a caixa e a vida.

### O custo e as capturas

O quadro inteiro (`--bench-frames 600 --adapter integrated`, 1080p, a Radeon integrada, medido junto com o executável da
main): a largada de sempre (-327,7, -17,7; o carro derruba um poste no começo) 6,71 ms contra 6,59 ms da main; a rua do
tráfego (-138, 555 com `--fire-trigger 703`: dez carros na rua, quatro objetos derrubados) 5,70 ms contra 5,46 ms da main
na mesma rota sem o tráfego (175 quadros por segundo; o tráfego e os objetos custam ~0,25 ms, o render deles 0,10 ms de
CPU). As capturas (`out/agent_drv_traffic/shots/` do checkout principal, fora do git): `paris_trafego_poste.png` (a rua
com o tráfego e o poste derrubado pelo carro: `--car-pos -138,20,555,-0.40 --fire-trigger 703 --drive-input "W:3.2"`),
`paris_trafego.png` (o tráfego nas duas pistas da avenida) e `paris_poste.png` (o poste da largada caído na rua).

### O que falta da fase 2 e a estimativa

Do mundo vivo ficam: o `SMissionManager` (as regras da missão e as suas listas de eventos, a largada, o
`EChangeMaxTraffic`), os outros estados do `AIGroundVehicle` (os inimigos, a direção automática, o trilho), as listas
dinâmicas da grade, o `RPathEngine`, o `RAnimEngine` (os estímulos: os pedaços dos objetos, as molas do carro), os
efeitos do `TriggerFX` (partículas, faíscas, marcas de pneu), os outros eventos dos gatilhos e o resto do `RSceneObj` (o
`UseArticle` com o modelo próprio e o `Render` pelo artigo; o port desenha a instância de origem). O tráfego, os
gatilhos e os objetos derrubáveis eram 3-4 dos 16-27 dias das fases 2-4, e o som da seção 17 outros 3-4: com as duas
seções na main, as fases 2-4 ficam em **10-19 dias**.

## 19. A fase 3: a missão de Paris (o `SMissionManager`, os eventos, o HUD mínimo)

O núcleo da fase 3: o gerente da missão do jogo, as regras e os eventos que elas e os gatilhos criam, os textos do
`.loc` e um HUD mínimo, para que `nf_driving --drive` comece Paris onde o jogo começa e rode as regras dela. Tudo portado
do MIPS do PS2 e conferido no interpretador (a parte 15 do `nf_driving_check`); o nightfire-cxbx não reconstruiu o
gerente (deu só os nomes de alguns campos e do `EventDynamicData`).

### Como o PS2 faz

O `WWorld::Open` constrói o gerente (`SMissionManager::Construct`, 0x205188: um `new` de 0x1b50 bytes em
`glbMissionManager`, 0x33a024), carrega as regras do `'Rule'` e as configurações do `'MSet'` do `'Map '` da pista
(`Load`, 0x205220), muda para a etapa 0 e começa (`Start`); depois o `WTriggerManager::Init`, cujo `Restart` dispara os
`EStartMission`. O `GameLoop_StartUp` cria o carro do jogador e aponta a vida dele para o gerente
(`PhysicsObject::SetHitPointLoc(manager + 0x378)`). A cada passo, o `ESimFrameUpdate` chama o `SMissionManager::Update`
(0x206b88) depois da IA e antes do `WTriggerManager::Update`: o modo de cinema, a vida (o "SUPER EASY" do bloco da troca),
o objetivo à vista e o "caminho errado" (`UpdateObjectiveSpecial`: o evento do programador 4), as ações temporizadas, os
perfis de seção e as regras (`ProcessRules`: a lista de sempre, depois a da etapa corrente; a etapa vira a próxima no
fim). Uma regra (`SMissionRule`, 15 classes: `Always`, `Ammo`, `Damage`, `Death`, `Health`, `PIP`, `Prog`, `Range`,
`Speed`, `Timer`, `Property`, `Collision`, `PlayerDir`, `Difficulty`, `ProgCounter`) dispara quando a `CheckRule` difere
do "invertida" e cria os eventos da sua lista `'el'` (`RunEvents`, 0x2095f8), os mesmos dos gatilhos.

Um evento é um objeto de 4 a 0x3c bytes num anel de 32 KB (`Event::operator_new`, 0x175730), criado pela função do seu
número (`RegisterEvent::LookupEvent`, a tabela 0x36fda0), que lê os dados da lista e o `EventDynamicData` (0x32e708). O
trabalho fica no destrutor, que o `EventManager::RunEvents` (0x175690) chama na ordem no fim de cada passagem do
escalonador; alguns construtores já agem (o `EChangeStage` muda a próxima etapa, o `ESetTimer`, o `EStartTimer` e o
`EStopTimer` mexem nos cronômetros). As listas `'el'` do CARP passam pela função de resolução de cada número (a tabela
0x32bc20), que troca as palavras de referência pelo `CARP::TagReference`.

### O gerente e as regras (`game/mission.h`)

`SMissionManagerData` tem o leiaute do PS2 (0x1b50 bytes, `static_assert`): as etapas, os 10 `SMissionTimer` (0x18), os 64
eventos do programador, o estado (1 rodando, 3 ganhou, 4 perdeu), a direção automática, a vida e o escudo do jogador, as
estatísticas, o modo de cinema, os 64 contadores mágicos, o objetivo (o alvo e o tipo: a posição, a instância, o elemento
da IA), as ações temporizadas e os 64 perfis de seção. As regras são a imagem do objeto do PS2 (`SMissionRuleData`: a
lista, ligada, uma vez, invertida, a tabela virtual da classe e os campos dela). Portados: o construtor, `Load`,
`Unload`, `ClearState`, `Reset`, `Start`, `Stop`, `Win`, `Lose`, `RestoreLives`, `ClearScoreBuffer`, `ProcessRules`,
`ChangeStage`, `CallStage`, `Update` e as suas partes, o modo de cinema, a direção automática, a vida e o escudo, as
estatísticas, os contadores mágicos, os objetivos, as mensagens, o `SMissionTimer` e as 15 classes de regra com os alvos
(`SMissionRuleTarget`). O bloco da troca (`uber_bond()`, os 0xa50 bytes de 0x100000) vem de `uber_bond_defaults`: o que o
`BASE.ELF` deixaria numa ida pelo mapa das missões (o nome da pista, `IDL_ENG.loc`, os volumes, a dificuldade).

Divergências (no cabeçalho e em cada função): as listas do PS2 (os nós do `FastAlloc`) viram vetores do port; o
`AIElementController` não existe, e o que as regras e os objetivos perguntam aos elementos da IA vem de `mission_ai()`
(vivos com vida 1000, parados no ponto do `'AIEl'`, sem propriedade: as regras de morte e dano de um inimigo esperam);
as chamadas a sistemas não portados (o `SWeaponManager` do `LoadWeaponSet`, a câmera, o som, o `AIElementController`, o
`GHud::Stylin`) são anotadas (`mission_trace()`) e contadas; o `SRulePlayerDir` (o `acos` da newlib) e o `SRuleAmmo` (a
munição do `SWeaponManager`) devolvem 0 e contam (nenhum dos dois aparece em Paris). Em Paris: 114 regras (12 na lista
de sempre, 102 em 47 etapas), 868 listas `'el'` (114 das regras, 754 dos gatilhos) com 1.465 eventos de 95 números.

### Os eventos (`game/events.h`)

`kEventTable` traz, para 97 números (os 95 das listas de Paris, o `EEndMission`, o `ERestart` e o 172, que divide a
criadora com o 171), o tamanho do `operator new`, a tabela virtual da classe, a criadora, a função de resolução, as
palavras dos dados que passam pelo `TagReference` e a origem de cada palavra do objeto (os dados ou o
`EventDynamicData`), lidos por execução simbólica das criadoras e dos construtores. `event_create` monta a imagem do
objeto (`PendingEvent::obj`, com as palavras escritas em `written`) e roda os construtores que agem; `events_run` (o
`RunEvents`) executa os destrutores portados na ordem: os do tráfego, dos gatilhos e dos objetos derrubáveis da seção
18, e os da missão e do HUD: 22 `EStartMission` e 140 `EResetPlayerCarPos` (o carro do jogador no gatilho, de frente
para ele, com o momento da velocidade do evento), 40 `ECallStage`, 41/48/49 (vazios), 47 `ESetTimer`, 44 `EDisplayText`,
45 `EPlayerLose`, 46 `EPlayerWin`, 53/54 os gatilhos, 68 `EEndCameraAnim`, 69 `ESetObjective`, 76/77/233/234 a direção
automática, 103/104 as tarjas, 112 `ESetSimRate`, 125 `ETwoWheelsOff`, 131 `EAddScoreAction`, 162 `EDamagePlayer` (o
`ApplyDamage` do carro, contado), 193/194 a mensagem de botão, 200 `ESetMagicCounter`, 219/220 a parada forçada,
225/228/229 os objetivos, 275/276 os acertos e as mortes. Os outros números são contados (`event_stats().skipped`). O
resolver do `'el'` no `carp_runtime.cpp` usa a mesma tabela (antes, só os quatro números do tráfego e dos gatilhos).

### Como Paris começa e anda

A missão começa na **seção do helicóptero** (o `'MSet'` pede o veículo `helicopter`; o port põe o Vanquish): o
`EStartMission` do gatilho 684 (779,1, -7,6, 207,0) põe o carro na largada; o 685 liga a direção automática pelo trilho
(`EControlToSpline`, não portado), as tarjas, a etapa 19 e o objetivo 2800. A seção acaba no gatilho 727 (-320,6, -10,
117,9), que um corpo de tipo 2 toca (um inimigo): mata os elementos da IA, arma o cronômetro 5 (4 s) e muda para a etapa
13; ali a regra 34 (o cronômetro 5 abaixo de 1 s) tira as tarjas e põe o carro no gatilho 683 a 35 m/s
(`EResetPlayerCarPos`), em cima do 687, que começa a **seção do carro**: o objetivo 2801, o cronômetro 3 (300 s,
regressivo), a etapa 27 e a 28. Os objetivos seguem pelos `ESetObjective` dos gatilhos 610, 612, 614, 615, 617...; a
missão falha pelos gatilhos 606 e 674 (`EPlayerLose`), pela morte do jogador (a regra 0) e pelo fim do cronômetro 3 (as
regras 110 e 111 levam ao gatilho 677, o `nukedeath`); é **ganha no gatilho 663** (-1076, -5,7, -2302,9), que só um corpo
de tipo 2 toca: o caminhão inimigo, guiado pela IA.

O que já anda: a largada (com o Vanquish no lugar do helicóptero, parado: a direção automática não existe), a seção do
carro (pulando a do helicóptero com `--fire-trigger 727@1`), os objetivos, o "caminho errado", as falhas pelos gatilhos
e o resultado no HUD. O que não: ganhar jogando (o caminhão é da IA), a seção do helicóptero (o `PHelicopter`, o fuzil, o
trilho), a morte pelo dano (o `PBondCar::ApplyDamage`, 0x189598, não portado: a vida do gerente só muda pelos eventos).

### Os textos e o HUD (`game/hud.h`, `render/font.h`)

`GHudData` (0x4f4 bytes, o leiaute do PS2) traz os campos do `GHud` que a missão e os eventos escrevem; portados os que os
mudam (`Reset`, `FadeInFromBlack`/`FadeOutToBlack`, `LetterBoxOn`/`Off`, `AimOff`, `KillButtonMessage`,
`ClearObjectiveMessage`, `TriggerMissionMsg`, `TriggerObjectiveMsg` com a fila de um, `TriggerButtonMsg`,
`TriggerTimerOn`/`Off`, `SetMissionPassed`/`SetMissionFailed`, `TriggerBloodCurtain`) e, em `GHud_Tick`, o que os
`Draw*` do PS2 fazem com o tempo (o escurecer, as tarjas em 30 quadros, as mensagens, a fila do objetivo, a mensagem de
botão). Os textos: o `.loc` (`LocaleFile`: o `LOCH`, a tabela `LOCI` buscada pelos 16 bits de baixo do rótulo, as cadeias
da língua corrente; `GSystem::LOCALE_Symbol` devolve "MISSING" sem o rótulo), o `data\gfx\locale\IDL_ENG.loc` do
`MISC.VIV`. O desenho é do port: `hud_view` dá as faixas e as linhas na tela do PS2 (640 x 448: as tarjas, o resultado, o
cronômetro, o objetivo, a mensagem de botão e a da missão) e o `nf_driving` as desenha com a fonte de depuração do disco
(`data\render\debug.sfn`: o `FNTS`, 95 caracteres de 12 bytes e uma imagem de 8 bits do SHPS, lida por `font_parse`) como
retângulos da interface do `DrvRenderer` (`DrvSprite`, um desenho por quadro, mistura alfa); as faixas da largura da tela
do PS2 vão de uma borda à outra da janela. O `GHud` do PS2 desenha com as galerias (`hudNTSC.gal`: as fontes, a vida, o
radar, as armas), que o port não lê.

### A conferência (a parte 15 do `nf_driving_check`, `--so missao`)

O `'Rule'`, o `'MSet'`, os gatilhos, os registros do `'AIEl'` e todas as 868+ listas `'el'` de Paris vão para a RAM do
interpretador com os ponteiros resolvidos trocados (os outros alvos, `'AICo'`, `'AISp'`, `'Cams'`, instâncias e cadeias,
em cópias de 0x40 bytes); o `AIElementController` emulado (os acompanhamentos com a vida, a posição e a velocidade de
`mission_ai()`), o carro do jogador do porte, a câmera, o escalonador, o `GHud` e o bloco da troca também. O original
roda com o código do jogo (o `GHud`, o `PBondCar`, o `AIElementController`, o `ERestart` no interpretador); ganchos só
para a memória, as cadeias, o `printf` de depuração, a `LookupEvent` (a criadora pela tabela do porte), o
`StepCheckHitWorld` (nada corta), o `LookupInt` e as funções dos sistemas não portados, que anotam a chamada.

| Caso | Casos | Diferentes |
|---|---|---|
| `Construct`, `Load` (Paris), `ChangeStage(0)`, `Start`: o gerente, as regras de cada lista, as configurações, o bloco da troca, as chamadas | 1 | 0 |
| `SMissionManager::Update` com estados sorteados (as etapas, os cronômetros, os eventos do programador, os contadores, as vidas, os elementos da IA, o carro, o objetivo, as ações temporizadas, as regras ligadas): o gerente, as regras, os eventos criados no anel (a imagem de cada objeto pelas criadoras originais), as chamadas externas na ordem | 4.000 | 0 |
| a criação de cada evento das listas de Paris e sorteados (a imagem e os construtores que agem) | 5.465 | 0 |
| os destrutores portados (31 números, pela tabela virtual do objeto do original): o gerente, os objetivos, o `GHud`, o bloco da troca, o carro, o escalonador, os gatilhos, as chamadas | 2.915 | 0 |

(Com `--so missao --casos 4000`; no `Update` 1.923 casos criaram 3.882 eventos e houve 6.883 chamadas externas (na corrida inteira, com o estado das outras partes, 2.004, 3.863 e 6.894); o interpretador roda
5,4 milhões de instruções.) A conferência achou no porte: os endereços das funções do `SWeaponManager` e do
`WTargetPicker` anotados (eram de outra numeração) e o `AMenuSound::Trigger` do fim do `PBondCar::DisableTwoWheelStunt`,
que o porte do carro não toca (anotado no `ETwoWheelsOff`). `tests/test_driving_mission.cpp` traz as propriedades
independentes (o cronômetro, as regras e as etapas, o fim, a imagem dos eventos, o `.loc`, as mensagens do HUD, a fonte).

### No `nf_driving`

`--drive` começa a missão: o bloco da troca, o `.loc`, o `GHud`, o gerente com as regras de Paris antes dos gatilhos, a
vida do carro no gerente, o `Update` no `ESimFrameUpdate`, os eventos no fim de cada passagem, o HUD desenhado.
`--free-drive` (ou `--car-pos` sem `--mission`, como as linhas de comando de antes) dirige sem a missão; `--no-traffic`
também a desliga (sem os gatilhos); `--difficulty 0|1|2`, `--no-hud`; `--fire-trigger N@s` dispara o gatilho N aos s
segundos do relógio do jogo (`727@1` pula a seção do helicóptero, `608@6` mostra uma mensagem, `606@7` falha, `663@7`
ganha). O relatório ganha as linhas `missao` (o estado, a etapa, a vida, as regras, os eventos, os cronômetros, o
objetivo, o HUD), `eventos_sem_porte` e `eventos_executados`.

### O custo e as capturas

O quadro inteiro (`--bench-frames 600 --adapter integrated`, 1080p, a Radeon integrada, junto com o executável da main):
a largada de sempre sem a missão (`--car-pos -327.7,20,-17.7,0.3155`) 6,74/6,78 ms contra 6,69/6,76 ms da main; a missão
na seção do carro (`--fire-trigger 727@1 --drive-input "none:4.3 W:2"`) 5,37 ms (outra vista; o gerente, as regras e o
HUD custam centésimos de ms). As capturas (`out/agent_drv_mission/shots/` do checkout principal, fora do git):
`start.png` (a largada, com as tarjas), `car8.png` (a seção do carro: `--fire-trigger 727@1 --drive-input "none:4.3
W:3.7"`), `message.png` (a mensagem "Wrong Way! Follow the Van" do gatilho 608), `lose.png` ("Mission Failed", o 606) e
`win.png` ("Mission Accomplished", o 663).

### O que falta para Paris e a estimativa

Os eventos de Paris que o port cria e não executa, por sistema (o número de vezes nas listas das regras e dos gatilhos):
a IA (147: 65 `EAICommand`, 66/67 ativar e desativar, 144/145 o fogo, 159, 161 `EKillAIElement`, 209, 232), o som (97:
20 `EStreamEvent`, as falas e a música, 164, 241/266 os mixes, 273), os efeitos (64: 9 `EPlayEffect`, 36, 130, 154
`ESpawnExplosionStatic`, 173, 196 `ENuclearBlast`, 250, 252/253, 257, 279), a câmera (42: 63 e 97 as animações dos
`'Cams'`, 204, 207, 242 `ECameraLockOn`, 246, 251), as armas (38: 25, 56, 60, 149/150 os power-ups, 192
`ESetSpecialWeapon`, 249 `EGadgetOn`, 269), a direção automática e os trilhos (36: 42/43, 108, 116, 153, 202
`EControlToSpline`, 215, 239), os filmes (4: 78 `ESetVideo`, 240 `ECustomTransition`). Para jogar Paris do começo ao fim:

| O que | Dias |
|---|---|
| o `PBondCar::ApplyDamage` (as zonas, os pneus, a vida: a morte pelo dano) | 1 |
| o `AIElementController` (os elementos do `'AIEl'`, os comandos `'AICo'`, os trilhos `'AISp'`), os estados de ataque e perseguição do `AIGroundVehicle` (o caminhão do fim, os carros que atiram) e os atiradores | 3-5 |
| o `SWeaponManager`, o `Missile`, os power-ups | 2-3 |
| a seção do helicóptero: o `PHelicopter`, a direção automática pelo trilho (`RPathEngine`), as animações de câmera | 2-4 |
| os efeitos (as explosões, o `ENuclearBlast`, o `PlayEffect`) | 1-2 |
| os fluxos de fala e música e os mixes (sobre a seção 17) | 1 |
| os filmes da missão (o `ESetVideo`) | 2-3 |
| o HUD de verdade (as galerias: as fontes do jogo, a vida, o radar, as armas) | 1-2 |

Paris inteira: **13-21 dias** além do feito. As outras missões reaproveitam quase tudo (os veículos, os métodos e os
eventos delas: 1-3 dias), e o gerente, os eventos e o HUD mínimo desta seção eram uns 2-3 dias das fases 2-4: com a
seção do helicóptero e os filmes, que a conta anterior não via, as fases 2-4 ficam em **14-24 dias** (eram 10-19).

## 20. A fase 2: os personagens (a pele, a animação e o ActPoser)

Os personagens das missões de carro desenhados e animados pelo caminho do jogo: o render method de pele lido no
microprograma da VU1, a biblioteca de animação da EA (a `libeaglanima`, o "EAGLAnim" do EAGL 4.06.01), o esqueleto e o
poser dos atores (`ActPoser`, `ActSkeleton`, `ActAnimGroup` do `ActActorDatabase.obj`). Tudo do MIPS e da VU1 do PS2,
conferido contra o código original nos interpretadores do port (a parte 16 do `nf_driving_check`, `--so personagens`, e
o `--verify` do `nf_driving`). O nightfire-cxbx (NightfireResearch) deu os nomes das funções do `ActPoser`, do
`ActAnimGroup`, do banco (`PrivateData::LoadBank`/`GetAnimation`, `FnDefaultAnimBank`), do `MemoryPoolManager` e a lista
dos tipos de canal (o `anim.md`/`eagl.md` deles); a leitura das funções é toda do PS2.

### A família de pele (`mat_char.h`)

Os modelos (`data\actors\models\*.dat` com o `.rel`) usam três métodos: `BondLitTextureAndShadow_Skin` (2.430 lotes nos 73
modelos dos nove arquivos), `BondLitTextureAndShadowSpecular_Skin` (70: o especular e o reflexo do carro) e `BondGouraud`
(3, os lança-granadas `ul_gr`, `fod_gr`, `ssm_gr`: fica no substituto). O microprograma, por lote: a paleta misturada (os
pesos e os três ossos de cada entrada da tabela dos pesos, a partir do qword 4 da VU, sobre a paleta em 0x22: o
`EAGLAnimationBuffer` de cada osso vezes a matriz de escala do modelo, que as operações 0xa, 0xb e 0xd do `GeoPrim::Draw`
mandam; o `Model_PaletteScaleMatrix` 0x002a6330 e o `Palette_Multiply` do port); os vértices únicos pela matriz deles e
espalhados nos lugares de saída; a cor (as três luzes do `GAME::ActorLightSources` pelas linhas normalizadas da matriz do
mundo, as quatro pontuais do `GAME::PositionalLights`; a sombra do `CharacterOptions.z` dá preto); a transformação (o
recorte do microprograma: o w 2049 liga o ADC do vértice que fecha um triângulo fora do volume) e, no Specular, as
passadas 2 (o especular pela `GAME::SpecularMat`) e 3 (o reflexo). Com `--verify`, cada lote dos personagens roda no
interpretador da VU1 e é comparado com a referência na CPU, também com uma câmera em perspectiva, uma matriz do mundo
girada, as luzes pontuais acesas e a sombra: 7.500 rodadas (2.500 lotes, 5.000 variantes), 349.474 vértices, nenhuma
diferença (STQ, cor, posição, ADC).

O estado do GS dos personagens vem de um símbolo `RUNTIME_ALLOC::EAGL::GeoPrimState` com a especificação no nome
(`SetAlphaBlendMode=EAGL::ABM_BLEND;SetAlphaTestMethod=EAGL::ATM_ALWAYS;SetCullEnable=true;SetTextureEnable=true;
SetPrimitiveType=EAGL::PT_TRIANGLESTRIP;`, nos 73 modelos; os quatro braços com arma têm também um com `ABM_ADD`). O analisador (0x0027fe80,
14 KB) foi portado nas chaves que os dados usam (`GeoPrimState_FromRuntimeSpec`): a faixa de triângulos, a mistura (Cs - Cd)
As + Cd (ou a soma), a textura, o teste ALWAYS sem ligar o teste e, como o construtor, sem névoa (o microprograma grava F =
0 no XYZF). Antes o port usava o estado padrão do `RStateManager` (lista de triângulos e névoa): metade dos triângulos sumia
e o FGE com F = 0 daria a cor da névoa inteira.

### A animação (`anim/eagl_anim.h`)

Os bancos (`data\actors\anims\*.dat`, o símbolo `AnimationBank`: +4 o número de animações, +0xc as animações, +0x10 os
nomes em ordem, achados pela busca binária 0x0026ddd0) guardam árvores de canais; cada animação começa pelo tipo do canal
e o `FnAnim` do tipo, criado pela fábrica (0x002647c8) num reservatório próprio, lê os dados (`SetAnimMemoryMap`) e escreve
a pose (`Eval(anterior, tempo, pose)`: 12 floats por osso, a escala, a escala do comprimento, o quatérnio, a translação).
Nos 89 bancos dos nove arquivos: 1.781 animações, todas compostas (o tipo 15); 379 só de eventos (o tipo 1: a segunda
animação do `ActAnimGroup`, os nomes `E_*`) e 1.402 de pose (`S_*`), com 332 canais `FnDeltaQuatChan` (11), 1.064
`FnKeyQuatChan` (13), 1.110 `FnDeltaF3` (20) e 270 `FnDeltaF1` (21). Portados (`// [driving]`):

- `FnCompoundChannel` (o composto): `SetAnimMemoryMap` 0x00266518, `Eval` 0x002666b8 (os subcanais criados na primeira
  avaliação, do último para o primeiro; com `UseFPS` os tempos vezes os quadros por segundo), `GetLength` 0x002663b0;
- o `DeltaCompressedData` (cada valor é o do quadro anterior mais um delta de 4, 8 ou 16 bits com deslocamento e escala
  por canal): `DecompressValues` 0x00276820 (do quadro em cache ao pedido, ou da base) e `DecompressValuesIndexed`
  0x00276c70 (o passo do quadro inteiro ao seguinte vezes t, nos índices da pose); os dados do disco têm só deltas de 8
  bits;
- `FnDeltaQuatChan` (`Eval` 0x00274d30, `SetAnimMemoryMap` 0x00276288, `GetLength` 0x002761e0): os quatérnios de todos os
  quadros; entre dois quadros a interpolação linear do indexado, sem normalizar;
- `FnKeyQuatChan` (`EvalSQT` 0x00275228, `SetAnimMemoryMap` 0x002765c0, `GetLength` 0x00276658): quadros-chave (a busca a
  partir da chave em cache, para a frente ou para trás) e o slerp rápido da VU0 (0x00298478, igual ao `VU0_fastqslerp`
  0x002cc018: q1 t ± q0 (1 - t) com o sinal pela distância das pontas, renormalizado pelo RSQRT);
- `FnDeltaF3` e `FnDeltaF1` (`InitBuffersAsRequired` 0x0026fc80/0x002713f0, `EvalSQT` 0x0026e4e0/0x002701b8,
  `SetAnimMemoryMap`, `GetLength`): três floats (a translação, a escala) ou um por canal em blocos de 2^n quadros (o
  primeiro com o valor inteiro em 16 bits, os outros com deltas de 8 bits pela tabela de dequantização), com ou sem
  quadros-chave, o quadro seguinte guardado num segundo buffer, e as constantes no fim (os canais que não mudam);
- `VU0_MATRIX4_mult` (0x00297c60 e a cópia da `librealmema` 0x002cbb88) e `VU0_SQTquattom4` (0x002985d0);
- o `Skeleton` (os `data\actors\skeleton\<n>skel.o`: +8 o número de ossos, +0xc as escalas de comprimento, os ossos de 0x70
  bytes com o pai, a pose de ligação e a inversa dela): `GetStillPose` 0x00262aa8, `PoseSQTToLocal` 0x002623c0,
  `PoseLocalToGlobal` 0x00262508 e `PoseGlobalToSkin` 0x002634f8 (a inversa da ligação vezes a global: o
  `EAGLAnimationBuffer`).

Quirks mantidos: o `DecompressValues` avança o ponteiro dos deltas só `nchan` valores por quadro (o jogo só pede um quadro
quando `nchan` é menor que o total); com deltas de 4 bits soma o primeiro canal ao começo em bytes; o indexado não soma
o primeiro canal ao ponteiro; os `FnDelta*` sempre andam para trás subtraindo deltas (o global 0x003441c0 vale 1 e nada o
escreve); com zero canais o `InitBuffersAsRequired` roda a cada chamada e só as constantes são escritas (a parte 16 achou
isso: o porte saía antes delas). O `VU0_charAnim` (o primeiro programa do `.vutext`, em 0x00324800) não é usado: o
`VU0_UploadMicrocode` só é chamado pelo `SimulateGame` e pelo `UploadCullingMicrocode`. Divergências (só do port): o
`MemoryPoolManager` (as listas livres por tipo e por classe de tamanho, o bloco de 0xc800 bytes) vira objetos C++; o canal
de eventos não roda na pose (o `RawEventChannel::Eval` 0x00274958 chamaria os tratadores pela tabela que recebe, e a pose
não é uma tabela: no jogo só a animação de pose passa pelo `Eval`; contado em `AnimStats::events`); o `EvalSQT` com máscara
(o dos misturadores) e os tipos da fábrica que os dados não usam não são portados (contados).

### O esqueleto e o poser (`anim/actor.h`)

O `ActSkeleton` (0x00114760: o Skeleton do `.o`, a pose parada, a pose, as matrizes locais, os ossos "Root.Root",
"Root.Weapon" (ou "Root.RWeapon") e "Root.Clip" pelo `GetBoneIndex` 0x00114a08), o `ActAnimGroup` (`ChangeAnimation`
0x0010c928: a animação de pose e a de eventos e os comprimentos) e o `ActPoser` no modo normal: `SetNormalAnimation`
0x001133d0 (o tempo em segundos vezes 20 quadros por segundo), `DoMainPose` 0x00113b48 (a pose parada, a animação avaliada
sobre ela entre o tempo anterior e o atual, `PoseSQTToLocal`, a raiz vezes a matriz `PoseToWorld` do `ActPoseMatrices`, sem
a translação x/z com `SuppressAnimationTranslation`), `TransformLocalPoseToWorldPose` 0x001141e8, `Skin` 0x00114218,
`AdvanceTime` 0x001145c0 (um passo de tempo de quadro x 20 x a escala do tempo, até o total - 1) e `ActActor::IsAnimationDone`
0x0010ac58. O tempo de quadro é o do `ActManager::StartUp` (o `GetFoundationVideoModeRate`); o port usa 1/60 s. A matriz do
mundo do desenho é a do `ActActor` (`BuildScale`: a escala do personagem vezes o `GetBaseScaleFactor`, 0,1: o esqueleto
está em decímetros).

O que não é portado: o `ActAnimationDatabase` (a lista de bancos do `.txt` da missão, `paris0.txt`, e a tabela
`ALookup.bin` de (banco, índice) para (pose, eventos)); os modos 2 (o crossfade) e 3 (a mistura manual) do `DoMainPose`; o
IK e as sobreposições globais do `DoSkeletonPose`; o `CalcSnapAndCorrectionMatrices` (a matriz nivelada com o chão, que
precisa do ator); os eventos; a arma no osso "Root.Weapon" (`ActWeapon`); o `ActCharacter::Draw` (o `CharacterOptions` do
quadro: o alfa, a escuridão da sombra).

### Os personagens do jogo

As cadeias do `ActCharacterInfo` (0x0010de98) estão no `.data` do ELF: "esqueleto.modelo.texturas.escala", o modelo com o
LOD no nome do arquivo (1 o mais detalhado) e as texturas no lugar das do `Actors.ssh` ('hhhh' a cabeça, 'tttt' o tronco,
'bbbb' o corpo: `ActTextureDatabase::LoadTexture` e `ActModel::SetTexture`). Em Paris (`MIS01.VIV`): `2.c_bondc.h_bond.t_bondc.100`
(Bond ao volante, o banco `driving`: `S_Driving_Bond_*`), `3.c_domm.h_dommin.t_dommin.100` (Dominique, a passageira:
`S_Driving_Zoe_Passenger_Loop`), `2.h_pmask.h_pmask.t_paris.95` (os capangas mascarados, `h_pmask1-3`, com os bancos `sr`,
`srb`, `srmg` e `swmg`: atirar, inclinar-se, ser ferido e morrer), `4.a_handc.b_handc.90` (as mãos de Bond) e
`6.a_snipe.b_snipe.70` (o braço do atirador), mais os pedestres do banco `ped`. Quem os põe na largada da missão é o
`SMissionManager` (as listas de eventos da missão, outra frente do port): para aparecerem lá faltam o `ActActor` (as
matrizes do ator a cada quadro), o `ActAnimationDatabase` e a arma.

### A conferência (a parte 16 do `nf_driving_check`, `--so personagens`)

O código original no interpretador MIPS (com a VU0 em modo macro) ao lado do porte, com os dados do disco: os 18 bancos
diferentes dos nove arquivos e os 9 esqueletos, cada objeto carregado duas vezes pelo `DynamicLoader` do porte (as palavras
que diferem pelo deslocamento das duas cópias são os ponteiros) e copiado para a RAM emulada; os canais criados pela
fábrica original num reservatório emulado e chamados pela tabela virtual. Com `--casos 4000`:

| O que | Casos | Diferentes |
|---|---|---|
| o slerp rápido (0x00298478 e 0x002cc018), quatérnios quase opostos e t fora de [0, 1] | 8.000 | 0 |
| `VU0_MATRIX4_mult` (as duas cópias, também no lugar) e `VU0_SQTquattom4` | 12.000 | 0 |
| `DecompressValues` (todos os canais, sequências para a frente, para trás e recomeçando; um grupo de uma chave à seguinte) | 18.000 | 0 |
| `DecompressValuesIndexed` | 1.396 | 0 |
| os canais pela fábrica: `GetLength` e `Eval` das 272 animações de pose (passos, saltos, antes do começo, depois do fim) numa pose de 128 ossos | 11.152 | 0 |
| o `Skeleton`: `GetStillPose`, `PoseSQTToLocal`, `PoseLocalToGlobal` (fora e no lugar), `PoseGlobalToSkin` | 4.500 | 0 |
| o `ActPoser`: `SetNormalAnimation`, `DoMainPose`, `TransformLocalPoseToWorldPose`, `Skin`, `AdvanceTime`, `IsAnimationDone` (133 pares esqueleto x animação) | 26.733 | 0 |

A conferência achou o caso dos `FnDelta*` sem canais (só constantes). O `nf_driving --check` também avalia cada animação
de pose de todos os bancos em 14 tempos (19.628 avaliações, nenhuma guarda, nenhum tipo sem porte).

### No `nf_driving`

`--actor <cadeia>` (a do jogo) com `--anim <banco>:<nome ou número>` desenha o personagem posado pelo `ActPoser` com a
animação tocando em laço a 60 quadros do jogo por segundo (o recomeço no fim é só do port: o controlador do ator); `--seconds`
dá o tempo da captura, `--list-anims` lista os bancos e as animações do arquivo. O vertex shader da família de pele refaz o
microprograma por vértice com as matrizes do quadro: a paleta misturada (até três ossos por vértice, as 128 matrizes do
`EAGLAnimationBuffer` num buffer de constantes), as luzes e as passadas do Specular (sem o recorte do microprograma). As
capturas (`out/agent_drv_chars/` do checkout principal, fora do git): `pmask_fire.png` (um capanga em `S_SRMG_Fire2` a 1 s),
`pmask_fire_02.png` e `pmask_fire_24.png` (o mesmo a 0,2 s e a 2,4 s), `domm_passenger.png` (Dominique sentada) e
`bond_driving.png` (Bond ao volante, o modelo do carro cortado na cintura).

### O que falta e a estimativa

O `BondGouraud` (os lança-granadas), o `ActActor` e o `ActCharacter::Draw`, a arma na mão, o crossfade entre animações, os
eventos das animações (os sons e os efeitos que elas disparam), o IK e o `ActAnimationDatabase` com o `ALookup.bin`. O
desenho e a animação dos personagens eram uns 2-3 dos 10-19 dias das fases 2-4; ficam 1-2 dias para eles, e as fases 2-4
ficam em **9-17 dias**.

Com a seção 19 (a missão, juntada à main ao mesmo tempo, que subiu a conta para 14-24 dias com a seção do
helicóptero e os filmes), as fases 2-4 ficam em **13-22 dias**.

## 21. A fase 3: a IA dos elementos de Paris e o dano do carro (o `AIElementController`, os agentes, o `ApplyDamage`)

Até a seção 19 a missão perguntava a um substituto (`mission_ai()`) pelos elementos da IA e o carro não perdia vida:
Paris não se ganhava nem se perdia jogando. Esta seção porta, do MIPS do PS2, os elementos (`AIElementController`), os
motoristas deles (o estado 5 do `AIGroundVehicle`, "agente": perseguir, fugir na frente, seguir a rua) e o dano do carro
(`PBondCar::ApplyDamage`), e liga os três à missão: a van da bomba foge pelas ruas de Paris, o EMP a para (a vitória),
ela chega ao fim do caminho (a derrota), e o carro do jogador morre pelo dano (a regra Death). O nightfire-cxbx
(NightfireResearch) deu os nomes dos métodos e o mapa dos campos do controlador; o código é o do PS2.

### Como o PS2 faz

O `'Map '` da pista traz três tabelas da IA: o `'AIEl'` (os elementos, 0x100 bytes: a matriz de partida, as bandeiras, a
dificuldade em que existe, o tipo, a vida, o nome do veículo, até três passageiros e o primeiro comando), o `'AICo'` (os
comandos, 0xa0 bytes: o alvo, os modos de navegação e de ataque, as velocidades, as armas, o trilho) e o `'AISp'` (os
trilhos, seção 23). Paris tem 25 elementos (todos veículos: a van da bomba, os caminhões e os carros dos capangas; 17
levam um passageiro que atira) e 61 comandos. O `AIElementController` (0x58 bytes em `fObj`, 0x327c30)
copia os registros e monta um `AIElementTracking` (0x50) por elemento; a cada passo da simulação atualiza os
acompanhamentos (a distância ao jogador, o quanto está à frente da câmera, os raios de 200 e 220 m), conta quem quer
acordar e acorda todos se cabem (32 personagens, 6 veículos, 16 helicópteros), senão os mais à frente da câmera (o qsort
do newlib, portado com a ordem dos empates). Acordar um veículo cria o `PBondCar` de classe 2 (um agente com motorista)
ou 4 (parado) no ponto, liga a vida dele ao registro (+0x4c, a que as regras leem) e cria o `EAICommand` do primeiro
comando; dormir guarda a matriz e apaga o carro; morto fica morto.

Um agente roda a cada passo `UpdateStuck`, `DoAgentStateUpdate` (o alvo: o secundário de outro elemento, a fumaça, a hora
de abalroar; o ponto-alvo adiantado pela velocidade do alvo; as trocas entre a rede e o direto), `DoAttackMode` (a
velocidade pedida: 13 perseguir o alvo, 14 manter-se à frente dele, 15 seguir a rede à velocidade da rua com o alvo
colado), `DoWeaponUpdate` (a torre e as armas do carro: o pedido de tiro) e `DoMoveMode` (1 parado, 2 direto com o
desvio pelas 64 faixas de través de 1 m, 3 e 4 pela rede com o desvio pelas faixas da rua, 5 pelo trilho), e dirige com
`DriveToPoint`. O navegador ganhou os modos dos agentes: 4 ao acaso (`CalcNextSegmentRandom`) e 3/5 rumo a um trecho
(`CalcNextSegmentDirection`).

O dano: `PBondCar::ApplyDamage(de, para, dano, divisão, tipo, assinatura)` acha o ponto do segmento na caixa do modelo
(`FindOBBIntersect`), a zona (16, `CARP::BaseDesc::CalcDamageZone` e a tabela 0x3c1fe8), gasta os pneus num tiro (com o
estouro), desconta do escudo (+0x434) e da vida (`LoseHitPoints`), conta os mortos e os acertos na missão (só com a
assinatura do jogador), empurra o carro num tiro (o torque sorteado pelo `SimRandom` através de `BytesToCoord`), sobe as
zonas +0x2f0 (o amassado e o rasgado que o `RVehicle` desenha) e manda ao render os estímulos e os efeitos. O
`AttackWithEmp` é o Q-Pulse: o carro fica parado 120 passos, e na `paris_bombvan` dá o `ProgrammerDefinedEvent(0)`.

### Os arquivos

- `game/bond_car_damage.cpp`: `ApplyDamage` (0x189598), `SetVisualDamage` (0x1876c8), `AttackWithEmp` (0x18ac28),
  `LoseHitPoints` (0x195a58), `FindOBBIntersect` (0x2d3950), `BytesToCoord` (0x2d0b08), `CalcDamageZone` (0x2db798) e
  as vizinhas (0x2db930). O que vai ao render e ao som (os `ProcessStimuli*` do `RAnimEngine`, o `RVehicle::TriggerFX`,
  o `GHud::TriggerDamageFlash`, o som do estouro) é gravado em `car_damage_stats().calls_out` e contado; a assinatura do
  PS2 vira `damage_source_is_player()` (quem atira a liga).
- `ai/element_controller.{h,cpp}`: o controlador e o acompanhamento (21 funções de 0x12af48 a 0x12dc18: `Construct`,
  `Update`, `WakeUp`, `GoToSleep`, `SyncRuleInfo`, `EveryoneChill`, `EveryoneToSleep`, `ForceVehicleToSleep`,
  `CalcMightAsWellBeDead`, `OkToSpawnHench`, `RedirectCarpElement`, `GetProperty`...) e o qsort do newlib (0x2a8198); o
  `PBondCar` de um elemento vem de `ai_car_factory()` (o `nf_driving` monta o modelo e o render).
- `ai/element_events.cpp`: os eventos da IA, pelos destrutores do PS2: 65 `EAICommand` (0x145ff8), 66/67 ativar e
  desativar (0x1476b8, 0x14b510), 144/145 o fogo (0x1469f8), 159 `EAIElementSetAccuracy` (0x146ca8), 161
  `EKillAIElement` (0x150748), 232 `ESetRecSpeedOffset` (0x1587f8); o 209 (`ESetNextAISpline`) e o 202/239 ficam no
  trilho (`game/rail.cpp`, que acha o veículo do elemento por `rail_element_vehicle`).
- `ai/ground_vehicle_agent.cpp`: o estado 5 (33 funções de 0x12e670 a 0x140110: `DoAgentStateUpdate`, `DoAttackMode`
  13-15, `DoMoveMode` 1-5, `DoWeaponUpdate`, `UpdateStuck`, `DriveToPoint`, `GetCollNav`, as faixas
  `GetDirectLaneWeights`/`GetRoadNetworkLaneWeights`/`GetBestWeightedLane`/`WeightDirectLanesPosRad`, os desvios
  `CheckAgentDirectCollision`/`CheckAgentRoadNetworkCollision`, `InCollisionRange`, `SetTargetMode`,
  `SetSecondaryTarget`, o `AIZoneController` das zonas de fumaça e óleo, a parte dos agentes do `AIVehicleController`);
  `ai/road_nav.cpp` os modos 3, 4 e 5 (`CalcNextSegmentRandom` 0x2235c8, `CalcNextSegmentDirection` 0x223a30,
  `PathShareRoadSegment` 0x226bd8, `PathForwardRoadSegment` 0x226c88).
- `ai/ai_weapons.h`: a ponte com as armas (de outro módulo): `DoWeaponUpdate` chama `FireBullets`, `FireRockets`,
  `FireMissiles` e `TurnTurret` por `ai_weapon_hooks()`; sem ganchos, só contam (`ai_weapon_stats()`).
- `game/mission.cpp`, `game/events.cpp`: a missão pergunta ao controlador de verdade (a vida, a posição, a velocidade,
  as propriedades `EMPTY`/`BLOWOUT`/`SMOKED`/`OILED`/`AWAKE`, as estatísticas, os mortos, o `EveryoneChill` do fim); o
  `mission_ai()` fica só para quando não há controlador (a conferência da parte 15).

Divergências (cada uma também na função): os personagens (os passageiros que atiram e os inimigos a pé:
`AICharacterEnemy*`, `ActActorDatabase`) e os helicópteros (`AIHelicopter`) não são criados (contados em
`ai_element_stats()`); o `DrivableVec`/`BarriersInPath` não chama o `WCollisionMgr::CheckHitWorld` (o original compara
o resultado com `< 2`, sempre verdadeiro: o teste não muda nada); o `IncNavPosition` para depois de 256 trechos (num laço
de trechos de comprimento zero o PS2 recorreria até estourar a pilha); o `InitAtPoint` numa célula da grade sem trechos
usa 0 como o t que o PS2 tira do lixo da pilha (`road_nav_stats().stack_garbage`); onde o PS2 leria memória inválida (o
alvo nulo de um `UpdateNavPos` com +0xb1, o do modo 14, um elemento fora da cópia) o port guarda e conta.

### Como Paris anda com a IA

Na seção do carro (o gatilho 687, a etapa 27, o cronômetro 3 de 300 s) a missão ativa os elementos 24 e 2; o 666
(tocado pelo jogador) ativa a van (o elemento 1, comando `'AICo'` 0: alvo o jogador, navegação 4, ataque 15) e o 688
(tocado pela van) lhe dá o comando 1 (ataque 14). O EMP na van (`AttackWithEmp` em `paris_bombvan`) dá o
`ProgrammerDefinedEvent(0)`: a regra `Prog(0)` da etapa 1 desliga os gatilhos de derrota, arma o cronômetro 5 (3 s), o
comando 52 para a van e a etapa 2 dá o `EPlayerWin` ("Mission Accomplished"). Sem o EMP a van chega ao gatilho 677 (ou
o cronômetro 3 o dispara): o `EControlToCPU`, as tarjas, a câmera e o comando 45 (o trilho até o 632) a levam embora, e
a etapa 29 dá o `EPlayerLose` ("Mission Failed"). A vida do carro do jogador a zero dispara a regra `Death` da lista 0
(o evento 45: "Mission Failed").

### A conferência (a parte 17 do `nf_driving_check`, `--so ia`)

`tests/driving_check_ai.inc`, incluído no `driving_check_traffic.cpp` para usar a máquina da parte 14 (a rede de
estradas copiada, a frota de seis carros na RAM emulada): o código original no interpretador MIPS com a VU0 ao lado do
porte, 0 diferentes em todos os casos (`--casos 4000`, as sementes 20261011, 7 e 123):

| O que | Casos |
|---|---|
| `FindOBBIntersect`, `BytesToCoord`, `CalcDamageZone` e as vizinhas, `InCollisionRange`, o `AIZoneController` | 4.000 cada |
| `InitAtPoint` nos modos 3 e 4 (pontos a quilômetros da rede também), `CalcNextSegmentRandom` 534, `CalcNextSegmentDirection` 466, `IncNavPosition` nos modos 3-5 (2.983 trocas de trecho), `PathShare`/`PathForwardRoadSegment` 1.000 cada | 11.358 passos |
| `ApplyDamage` (os quatro tipos, os pneus, o escudo em um terço, a imunidade, os submarinos e o helicóptero, as chamadas para fora comparadas em ordem), `SetVisualDamage` 195, `AttackWithEmp` 110 | 695 |
| o construtor do controlador sobre o `'AIEl'` de Paris, os acompanhamentos 334, o `Update` 365 (o `SpawnCarObject` sem lugar), `CompareInRangePrio`, `RedirectCarpElement`, `CalcMightAsWellBeDead`, `OkToSpawnHench` | ~1.100 |
| `AIGroundVehicle::Update` no estado 5 em sequências de até 15 passos (os carros da frota como agentes, o jogador como alvo, as armas pedidas: 875) | 3.676 passos |
| as partes uma a uma: `DoAgentStateUpdate`, `DoAttackMode`, `DoMoveMode`, `DoWeaponUpdate`, `UpdateStuck`, os dois desvios, `DriveToPoint`, `GetCollNav` | ~1.000 |
| `EAICommand` com os 61 comandos de Paris (sem os de trilho), `ESetRecSpeedOffset` | 683 + 206 |

Ela achou no porte: o `DoAgentStateUpdate` pede o `GetCollNav` do alvo, não o do próprio carro; o
`CheckAgentDirectCollision` com o +0x94 ligado pula só os pesos (o ponto ainda é desviado); o `GetCollNav` compara a
faixa guardada (`lbu`) com a pedida com sinal (uma faixa negativa refaz sempre); o laço de trechos de comprimento zero
e o lixo da pilha do `InitAtPoint` (acima). Os passos em que o original leria memória inválida ou o lixo da pilha (85 de
3.761 nas sequências) encerram a sequência sem comparar. Fora da conferência: o `WakeUp` que cria um carro e o
`GoToSleep` (que apagam carros da frota; jogados no `nf_driving`), os eventos 66/67/144/145/159/161, o
`GetProperty` e a assinatura do jogador no `ApplyDamage`. `tests/test_driving_ai.cpp` (9 testes) confere propriedades
independentes sobre dados montados à mão.

### No `nf_driving`

`--drive` na missão roda o controlador, os agentes e o dano; o relatório ganha as linhas `ia` (os elementos acordados
por tipo, os carros, os comandos, os personagens e helicópteros que o PS2 criaria, as armas pedidas, e cada elemento
acordado: a posição, a vida, a velocidade, os modos, a velocidade pedida, o trecho, a distância ao alvo, os controles e
o ponto para onde dirige) e `dano` (as chamadas, os acertos, o escudo, os pneus, os mortos, os estímulos e efeitos, a
vida do jogador e as zonas). Depuração (as armas do jogador e os inimigos de fora são de outros módulos): `--car-reset
s@x,y,z,giro` recoloca o carro do jogador; `--autopilot e[@x,z;...]` dirige o carro do jogador pelo controle do PS2 pelos
pontos e depois atrás do veículo do elemento `e`, pelo rastro dele (a linha `piloto`); `--kill-element e@s` (o
`EKillAIElement` dos alvos da seção do helicóptero, que o `--fire-trigger 727@1` deixa vivos); `--emp-hit s` (o Q-Pulse no
carro mais perto à frente); `--damage-hit s@dano` (um tiro no carro do jogador pelo `ApplyDamage`); `NF_DRV_AI_LOG=1` o
estado da IA a cada segundo. Os três finais, jogados sem `--fire-trigger` depois da largada (capturas em
`out/agent_drv_ai/shots/` do checkout principal, fora do git, 1280x720):

```
nf_driving --data . --track paris_mis01 --car vanquish --drive --fire-trigger 727@1 --kill-element 3@2 --kill-element 10@2
  --kill-element 11@2 --kill-element 23@2 --car-reset 8@-1143,-7,-1290,3.1416
  --autopilot "1@-1143,-1335;-1142.7,-1407;-1130,-1440;-1132,-1615" --emp-hit 34.5 --drive-input none:42   # win4.png
  ... o mesmo sem o --emp-hit, --drive-input none:47                                                       # lose4.png
nf_driving ... --fire-trigger 727@1 --kill-element ... --autopilot 1 --damage-hit 12@1000 --damage-hit 13@1000
  --damage-hit 14@1000 --drive-input none:17                                                               # dead4.png
```

`win4.png`: o piloto segue a van pela ponte, o EMP aos 34,5 s a para, "Mission Accomplished" (o estado 3, a etapa 2);
`lose4.png`: a van chega ao 677 e foge pelo trilho, "Mission Failed" (o estado 4, a etapa 29); `dead4.png`: o carro do
jogador amassado (as zonas no máximo, o vidro rasgado) e "Mission Failed" pela regra Death.

### O custo

Não medido nesta seção (o quadro inteiro com `--bench-frames` fica para a medição da integração).

### O que falta e a estimativa

Dos elementos: os personagens (os passageiros que atiram das janelas e do teto, `AICharacterEnemyWindow/Sunroof/Driver`,
os inimigos a pé, o `ActActorDatabase`; sobre a seção 20) e os helicópteros inimigos (`AIHelicopter`,
`SpawnHelicopterObject`); o `DoAutoDriveUpdate` (o estado 2, a direção automática pela rede depois do evento 42); o modo 2
do navegador (`CalcNextSegmentSidewalk`, os pedestres). Das armas (de outro módulo): o que as armas do carro dos agentes
fazem quando pedem tiro, e as armas do jogador que geram as zonas de fumaça e óleo e o EMP de verdade. A linha da IA da
seção 19 (3-5 dias) e a do `ApplyDamage` (1) saem, e entram os personagens e os helicópteros inimigos (2-3): Paris fica
em **10-16 dias** (eram 12-19) e as fases 2-4 em **10-17 dias** (eram 12-20).

## 22. A fase 3: as armas do carro e os efeitos de Paris

As armas do jogador e o caminho até o tiro: o `SWeaponManager`, a fila de ações dele, o motor de animação das instâncias
(o `RAnimEngine`, por onde o tiro passa), os eventos das armas e dos power-ups, o tiro (`EFireWeapon`), o míssil, a
explosão, o alvo do mundo do `WTargetPicker` e o tremor da câmera pelas explosões. Tudo do MIPS do PS2, conferido no
interpretador (a parte 18 do `nf_driving_check`: `--so armas` as armas, `--so misseis` o resto).

### Como o PS2 atira

O `GameLoop_StartUp` cria o gerente (`SWeaponManager`, um `new` de 0x190 bytes em 0x33a530; a tabela das 32 armas, 0xa80
bytes, vem do construtor estático 0x20f2b8: o nome, os rótulos, o pente, o dano, o banco e o nome do som; o intervalo de
cada uma vem de 0x33a030 x a taxa do vídeo) antes do `WWorld::Open`, cujo `SMissionManager::Load` carrega as armas da missão
(`LoadWeaponSet`: Paris dá a 2, o `Missile`, com 30 e o pente de 4, e a 26, o `Sniper Rifle` de Bond na seção do
helicóptero, com 24). O fim do `GameLoop_StartUp` liga o gerente ao `RSceneObj` do carro (`SetPlayerCar`). As ações do
`.def` chegam à fila "WeaponManQ" (`ActionQueue`, 200 entradas: o `ActionQueueManager::FeedQueue` manda cada ação a todas
as filas); no `drivecfg.def` o R1 é o `GAMEACTION_FIRESECONDARY` (os mísseis), o L2 o `FIREGADGET` (o especial: o
`ESetSpecialWeapon` de Paris põe o 11, o `Q-Smoke`), o R2 troca a secundária. A cada passo o `ESimFrameUpdate` chama o
`SWeaponManager::Update` logo depois do `SMissionManager::Update`: as ações, o tiro (`FireWeapon`: o prazo do cano, a
munição, o pente, e o estímulo 10 na animação do sistema da arma, `DoWeaponAnim`), a recarga cano a cano (o estímulo 11),
os estímulos 8 e 9 que abrem e fecham os lançadores das armas carregadas, o desfoque do turbo, a mira.

O tiro não cria o míssil: o estímulo vai para o `RAnimEngine::Handle` do carro (+0x28 do `RSceneObj`), que toca a animação
do lançador, e uma chave dela cria os eventos da lista `'el'` (o `EFireWeapon`, número 10, que o `Simulation::SpawnMissileObject`
transforma no míssil; o `EHideInstance`, 11, que esconde o míssil do lançador; na recarga o `EShowInstance`, 12, o mostra).
O Vanquish de Paris tem 16 sistemas: os quatro canos do míssil (7, 8, 0x34, 0x35; o 0x34 e o 0x35 com os eventos), a
metralhadora (9: o `EMuzzleFlash`, 31, e o `EFireWeapon`), o turbo (0xd, 0xe: o `EPlayEffect`, 9), o EMP (0x24), e os
quatro da morte do carro (0, 0x27, 0x39, 0x3a: o estímulo 2 cria o `EPlayEffect` e o `ESpawnExplosion`, 33).

### O motor de animação das instâncias (`render/anim_engine.h`)

O `RSceneObj::UseInstanceList` cria um `RAnimEngine::Handle` (`Handle::Create`): um bloco com as cópias das instâncias
(0x40 cada), dos estados de dano (`'di'`, 0x20), dos efeitos dos artigos (`'ef'`, 0x40, os que as bandeiras 0x109e pedem),
os sistemas (0x40, um por identificador de sistema dos artigos, ordenados; o `Create` grava os identificadores no próprio
artigo, +0x1a/+0x1b) e as zonas de dano de cada sistema (`CARP::BaseDesc::CalcDamageZone`). Cada artigo traz em +8 as
animações (`CARP::AnimInfo`, os elementos de 0x10 do `'an'`: as chaves `'aq'`, o sistema, os quadros, as bandeiras, e a
chave de busca (estímulo, estado de partida), com o estado de chegada). O `System::ProcessStimuli` acha em cada instância a
animação de (estímulo, estado) e põe o sistema na lista dos que tocam (256, 0x3d1518); tocando, o estímulo espera numa fila
de 1 ou 4. O `RAnimEngine::Update` (do `EAnimUpdate`) anda cada sistema: as chaves que o tempo passou criam os eventos delas
(o `EventDynamicData` com a cópia da instância e o índice, depois do `RSceneObj::SetEventDynamicData` do dono), e no fim o
sistema vai para o estado de chegada e o próximo estímulo da fila começa. O tempo do motor é o passo da simulação x
`RTimeData::fFrequency` (1/60) x 60, truncado pelo `fptoui`: com as contas do EE alguns passos dão um tique a menos.
O `RAnimEngine::EvaluateInstance` (o `gProcAnimFunc` dos tipos 0x00-0x63, que o `GetInstanceMatrix` chama) dá a matriz de
uma cópia na animação dela (o slerp rápido das chaves e a posição interpolada); o `RVehicle::Render` desenha as cópias do
Handle e o `DrawInstance` pula as escondidas (o bit 1 de +0xc).

### As armas (`game/weapons.h`)

`SWeaponManagerData` (0x190 bytes), `WeaponEntry` (0x54) e `ActionQueueData` (0x974) têm o leiaute do PS2. Portados: o
construtor, `Reset`, `CalcWeaponPriorities`, `SortWeaponList` (a bolha a partir de cada índice, como o original),
`FindSortedSecondary`, `SetPlayerCar`, `SetSpecialWeapon`, `Update`, `LoadWeapon`, `UnloadWeapon`, `SetPrimary`,
`SetSecondary`, `SelectSecondary`, `Get{Previous,Next}SecondaryType`, `Fire{Primary,Secondary,Special}`, `DoWeaponAnim`,
`FireWeapon` (o lançador dispara dois canos de cada vez; com o `EPowerUpMultiDamage`, o pente inteiro), `GetWeaponFromSystem`,
`GetSystemFromWeapon`, `UserReloading` e `HandleClipUpdate` (só com as mãos de Bond, que o porte não tem), `UpdateRadarDish`,
`StartNuclearSequence`, e a fila (`ActionQueue`). Os eventos: 25 `EPowerUp` (o efeito "Pr_Dstry", o objeto escondido, o
estímulo 10 da animação dele), 56 `EPowerUpAmmo`, 60 `EPowerUpMultiDamage`, 149/150 `EDisablePowerUp`/`EEnablePowerUp`,
192 `ESetSpecialWeapon`, 249 `EGadgetOn`, 269 `EUnloadWeapon`, 11/12 `EHideInstance`/`EShowInstance`.

Quirks guardados: começando do cano 3, o segundo míssil do par grava o tempo em +0x34 (o dano) da entrada (o PS2 não
confere o cano); a secundária -1 (depois do `EUnloadWeapon`) lê a entrada -1 da tabela (outra memória do heap no PS2; aqui
uma arma vazia, contada como guarda).

Divergências: as chamadas a sistemas não portados (o `AMenuSound`, o `WTargetPicker` (menos o alvo do mundo, abaixo), o `RPlayerCamera`, o
`AIZoneController`, o `RColorize`, os clarões do `RVehicle::TriggerFX`, os métodos do `PBondCar` das armas especiais, o
`GFX::Trigger`, o `RSceneObj::Hide/Show`, o `Simulation::TrackInstance`) são anotadas (`weapons_trace()`) e contadas;
o `UpdateTurret` (só com `HAS_TURRET`) e o `HandleNuclearSequence` depois do disparo ficam pendentes.

### A conferência (a parte 18 do `nf_driving_check`, `--so armas`)

| Caso | Casos | Diferentes |
|---|---|---|
| a tabela das armas do construtor estático, os intervalos, a ordem, os sistemas | 32 | 0 |
| o construtor do `SWeaponManager` | 1 | 0 |
| o `SWeaponManager` com estados e ações sorteados (`Update`, `FireWeapon`, `LoadWeapon` e os outros) | 4.000 | 0 |
| os destrutores dos eventos das armas e dos power-ups (10 números) | 2.000 | 0 |
| o `RAnimEngine` sobre o CARP do Vanquish: o `Handle::Create`, os estímulos, o `Update` (com os eventos das chaves), o `Stop`, as consultas, o `EvaluateInstance` | 4.000 | 0 |

### O tiro, o míssil e a explosão

O evento que a chave do lançador cria, o `EFireWeapon` (10), faz o tiro no destrutor (`game/fire_weapon.cpp`, 0x14ca60):
o lugar e a direção vêm da cópia da instância no `RSceneObj` do carro (`RSceneObj::GetInstancePosition`: a matriz do
objeto, o `SetProcAnimState` e a `GetInstanceMatrix` da cópia; a linha 3 é o lugar, a 2 a direção), a arma do sistema da
instância (`GetInstanceSystemID`, `SWeaponManager::GetWeaponFromSystem`), quem atira (o dono +0x24 do `RSceneObj`, a
assinatura dele, a velocidade do corpo), o alvo (o `WTargetPicker` do jogador), o dano (a tabela das armas), e o switch
das armas: os mísseis e os foguetes (1, 2, 0xe, 0x15, 0x16, 0x19, 0x1c, 0x1e, 0x1f) pelo `Simulation::SpawnMissileObject`
com o `RSceneObj::UseInstance` (o modelo do míssil é a cópia da instância do carro), o 0x16 e o 0x1f guiados pelo jogador
(`SMissionManager::SetDriveMissile`). Depois o som (o `AIndex::Lookup` de "SFX_<nome>Fire" no banco "Weapons" e, por arma,
um `AOneShotSound` em "Weapons Fire" para o jogador ou "Henchmen Fire", 50 a 200 m, com a velocidade de quem atirou; ou o
`AMenuSound` de "POV Weapons"), o tranco no controle (`ApplyJolt(1,0)`, ou `SetRumble(0,51, 0,2)` nas balas) e a zona da IA.
Um contador no dono (+0x64) conta os tiros para o som do rastro das balas (o `BulletStreakCount` do `RBulletStreak`).

O `Missile` (`game/missile.h`, 0x730 bytes no PS2) é um `PhysicsObject` de tipo 6 com um corpo simples de tipo 4
(`physics/simple_body.h`: o `SimpleRigidBody` de 0x40 bytes e as 96 vagas da `Simulation`, as assinaturas com o bit 31, o
`DetectCollisionsSRB`, que marca num `SimCollisionMap` de 144 bits os corpos que cada corpo simples toca: pelos raios, ou
pelas caixas orientadas). O construtor dá a cada arma a velocidade máxima, a aceleração, a curva, o tempo em linha reta, a
vida e o som do voo (um `ASound` em laço); sem alvo, o alvo é o do mundo (`WTargetPicker::GetWorldTarget`: um `WTargetable`
novo no ponto 175 m à frente do carro do jogador, que o `UpdateSelection` refaz a cada quadro desenhado; num submarino o
choque desse segmento com o mundo). A cada passo (`Missile::Simulate`): o `SteerMissile` (o slerp rápido da VU0 para o
alvo, a aceleração até a velocidade máxima, o zigue-zague dos mísseis do jogador, o míssil guiado), e o
`CheckForCollision`: a vida que acaba, os corpos que o `DetectCollisionsSRB` marcou (os rígidos pelas caixas: 0,1 x 0,1 x
0,5 do míssil; um carro do jogador leva o `AddDamageByPlayer` e o `ApplyDamage` de 32), e o mundo entre a posição de
antes e a de agora (`WCollisionMgr::CheckHitWorld`, `physics/world_hit.cpp`: as barreiras das instâncias, a face do chão
pelo `FindClosestFace` ao longo do segmento e o `IntersectSegPlane`, os cilindros e as caixas dos objetos; o mais perto do
começo). O míssil do jogador quase horizontal vai rente ao chão (0,4 m acima da face sob ele). Ao bater, o
`Simulation::SpawnExplosion` cria o `ESpawnExplosion` (33) e o míssil fica dois segundos morrendo (escondido, mudo).

O destrutor do `ESpawnExplosion` (`game/explosion.h`) acha o lugar e a orientação (o efeito, o `RSceneObj`, ou o lugar e a
direção do evento), escolhe o efeito da galeria (os nomes "Ex_Small", "Ex_SmB0", "Ex_SmallD", "Un_Expl", "FODHitEffect"...
pela arma, a face, o dano, o carro do jogador debaixo d'água), dá o tranco no controle do jogador perto (até 20 m), cria o
`Explosion` (`Simulation::SpawnExplosionObject`: um `PhysicsObject` de tipo 3 com um corpo simples de tipo 1, raio 0, que
cresce 1 m por passo), e os sons (um anel de quatro vagas de 0,3 s: "SFX_HugeExp"... na mistura "Explosions" e o
`ALimitedSound` "SFX_DistExp" em "ExplosionBass"). A cada passo o `Explosion::Simulate` empurra cada corpo que a onda toca
pela primeira vez (8000, 9000, 8000 x o dano, limitado a 25 x a massa; um giro sorteado pelo `SimRandom`) e chama o
`ApplyDamage` do dono (a entrada +0x1c da tabela virtual: o `PBondCar::ApplyDamage` dos carros, de outro agente; aqui
anotado e contado), até o dano cair a 1. O `ESpawnExplosionStatic` (154) cria um `ESpawnExplosion` no lugar de um gatilho.
A câmera do jogador treme com as explosões perto do olho (`RPlayerCamera::CheckForCameraShaking`: o raio dano x 5 contra
o quadrado da distância, o tremor e o tempo pelas constantes do `camera.ini`).

Quirks guardados: o `SimpleRigidBody::CheckCollisions` contra um corpo de helicóptero lê o +0x13c do dono do próprio
corpo (numa explosão, de 0x98 bytes, isso já é o lixo do heap no PS2: o port lê 0); o `Explosion::Simulate` compara o
quadrado da distância com o raio; o tom do som do voo volta sempre a 1 (o +0,01 só vale até 0,04); o `WorldCollisionInfo`
montado na pilha deixa o +0x4c, o +0x56 e o +0x5c com lixo (copiado junto com o choque; ninguém os lê).

Divergências e o que não foi portado (contado em `fire_stats()`, `missile_stats()`, `effects_stats()` e anotado em
`effects_trace()`): o desenho dos efeitos (a galeria: `GFX::LookupReference` e `GFX::Trigger`, o sistema de partículas
`RParticleSystem`, a queimadura `RScorchWorld::ScorchGround`, as luzes `RHighLevelLightManager::AddExplosion/AddMissile`);
o rastro dos mísseis (`RMissileStreak`: o Vanquish não o desenha, só as outras armas); os projéteis que não são o `Missile`
(as balas `Shell`/`RayShell` da metralhadora, o canhão, a granada, as minas, o `Newton` dos pedaços); a mira com alvos (o
`WTargetPicker` só tem o alvo do mundo: a trava nos veículos da IA, o cursor e o `GetSelectedTarget` esperam a IA); no
`EFireWeapon`, o efeito do evento (`LocateFX`), o veículo da IA, a pontaria da IA, a mira pela câmera da direção automática
e o helicóptero; as janelas que quebram (`CheckHitWindow`), o dano das instâncias do mundo (`Shell::ApplyWorldDamage`), o
`AMenuSound`, o `AIZoneController`. O `PBondCar::ApplyDamage` é do agente da IA (aqui a chamada é anotada). Os eventos de
efeito 9 `EPlayEffect`, 36, 130, 173, 196 `ENuclearBlast`, 250, 252/253, 257 e 279 são contados (a galeria).

No `nf_driving --drive`, a seção do carro (`--fire-trigger 727@1`) com o R1 (Ctrl no teclado) dispara o par de mísseis do
Vanquish (`--drive-input "none:4.3 W:1 W+R1:0.1 W:3.5"`): o modelo do míssil voa à frente (pequeno: 35 cm), segue o alvo
do mundo, bate no asfalto a 120 e 170 m e explode (duas explosões criadas e apagadas, o som do tiro a +7 dB no quadro do
tiro e as explosões a -17 dB RMS três segundos depois, `--audio-dump`); a linha `tiros ... | misseis ... | explosoes ...`
e a linha `misseis` do relatório. Quando o `ECustomTransition` troca o carro (o `ChangeCarType` da seção 23), o RVehicle
novo ganha o seu `Handle` e o `SetPlayerCar`.

### A conferência (o fim da parte 18, `--so misseis`)

Com `--casos 4000` (o heap emulado é devolvido depois de cada `CheckHitWorld` original: os `vector<>` das listas de
instâncias e de objetos que ele monta a cada chamada vêm dele):

| Caso | Casos | Diferentes |
|---|---|---|
| os métodos do `SimpleRigidBody` (o `UpdatePosition`, o atrito, a aceleração, os eixos, a orientação) | 9.000 | 0 |
| o `DetectCollisionsSRB` em cenas sorteadas (corpos rígidos e simples, tipos, bandeiras, caixas, o helicóptero) | 1.000 | 0 |
| o `SpawnExplosionObject` e os passos do `Explosion::Simulate` (com o `UpdatePosition` e o `DetectCollisionsSRB`) | 1.000 + 6.734 passos | 0 |
| os destrutores do `ESpawnExplosion` e do `ESpawnExplosionStatic` (as anotações, os sons criados, os globais do anel) | 2.000 | 0 |
| o `CheckHitWorld` em Paris, snow1a, junglea e snow2a (as barreiras, o chão, os cilindros, as caixas) | 10.000 | 0 |
| o `WTargetPicker::UpdateSelection` (o alvo do mundo) | 503 | 0 |
| o `SpawnMissileObject`, o `SetUserSteerX/Y` e os passos do míssil em Paris (com os eventos e as explosões que ele cria) | 1.000 + 85.058 passos | 0 |
| o destrutor do `EFireWeapon` (o tiro do carro do jogador, os sons, o tranco, o míssil criado) | 2.000 | 0 |
| (na parte 8) o `UpdateCamera` com 29 explosões perto do olho (o `CheckForCameraShaking`; 245 quadros com o tremor delas) | 2.400 quadros | 0 |

### O que falta e a estimativa

Das linhas da seção 19: "o `SWeaponManager`, o `Missile`, os power-ups" (2-3 dias) está feita menos as balas da
metralhadora (`Shell`, `RayShell`) e a trava nos alvos da IA (0,5-1 dia, com a IA); "os efeitos" (1-2 dias) está feita na
física, nos eventos, nos sons e na câmera, mas o desenho deles é maior do que a conta via: a galeria dos efeitos e o sistema
de partículas, a queimadura e as luzes dinâmicas são 2-4 dias. Paris continua em **12-19 dias** e as fases 2-4 em
**12-20 dias**.

## 23. A fase 3: a seção do helicóptero de Paris (o trilho, a câmera do AutoDrive, as animações de câmera)

O começo de Paris como no PS2: o carro da missão levado pelo trilho, a câmera de mira do AutoDrive, as câmeras animadas
do mundo e do trilho, os eventos de câmera e do trilho e a troca para o Vanquish no fim da seção. Tudo do MIPS e da VU0
do PS2, conferido contra o código original no interpretador (a parte 19 do `nf_driving_check`, `--so helicoptero`). O
nightfire-cxbx (NightfireResearch) deu nomes (`RPathEngine`, `AISplinePath`, `RCameraSpline`, as funções do
`RPlayerCamera` da direção automática) e a leitura do Xbox ajudou a separar as funções sem nome; a leitura é toda do PS2.

### Como o PS2 faz

**Não há `PHelicopter` no começo de Paris.** O `'MSet'` da pista pede o carro `helicopter` (o `SECONDARY_TYPE` dele é
o `vanquish`): é um `PBondCar` comum, de modelo invisível nas listas de vista, levado por um trilho; o `PHelicopter`
(0x00193408 e vizinhos) só existe para os helicópteros da IA (`AIElementController`), que esta seção não cria. O
gatilho 685 da largada roda o `EControlToSpline` (202) com o `'AISp'` 0, o `ESetAutoDriveCamOn` (77) com -60 e -20 graus,
o acelerador do trilho (116), a etapa 19 e o objetivo 2800.

- **O trilho.** O `EControlToSpline` pega o `AIGroundVehicle` do carro do jogador pela tabela virtual
  (`GetAIGroundVehicle`) e chama `EngageSplinePath`: a classe 1 (a CPU, `SetCarClass`), os controles zerados
  (`InitializeCarControls`), o estado 3 e o `AISplinePath` (0xb0 bytes: uma instância que o `RPathHandle` move, o estado
  de caminho do `'AISp'` e a matriz de aplicação `AISpline::GetApplyTransform`). A cada passo o `EAnimUpdate` roda o
  `RPathEngine::Update` (o tempo é o passo da simulação), o `AIGroundVehicle::Update` no estado 3 roda o
  `DoSplinePathUpdate` (o corpo posto na pose do caminho com `RigidBody::ResetObject`, a velocidade do `RPathHandle` e o
  momento) e o `PBondCar::Simulate` da classe 1 roda o `ProcessSplinePhysics` (as rodas: a velocidade e o giro pela
  velocidade do caminho, a derrapagem pelo ângulo com a frente, o esterço pelo lado, as molas pela altura do chão sob
  cada roda; nada com `SUB_PHYSICS`, sem molas com `IS_SNOWMOBILE`). Os eventos do trilho: 116, 153 e 215 (o acelerador,
  o tempo e o acelerador com espera e aceleração, pelo `EPathUtil_GetFromInstanceOrAI`), 209 (o caminho seguinte), 108
  (a velocidade da direção automática, `AIGroundVehicle` +0x134) e 239 (`DisengageSplinePath`: a classe 0, o estado 1, o
  ataque 1 e o `SetNavigateMode(1)`, que destrói o trilho). Os 42 (`EControlToCPU`: `EngageAutoDrive`, a classe 1, o
  estado 2, o ataque 15, a navegação 4 com o `WRoadNav` posto no corpo) e 43 (`EControlToPlayer`: `DisengageAutoDrive`,
  `SetTargetGas(0)` e `SetTargetBrake(0)`, o `IOModule` +4) são a direção automática pela rede de estradas: em Paris o
  42 só está no gatilho 677 (o fim do cronômetro: o carro levado à explosão) e numa regra de programador da etapa 27;
  o estado 2 (`DoAutoDriveUpdate`) é da IA e não foi portado, o carro fica sem quem o dirija até o 43.
- **O `RPathEngine`.** O `CARP::PathInfo` (os canais lineares, os quatérnios com slerp, os escalares, os em degrau e as
  Bézier com a rotação em Euler pelo `VU0_EulerToQuat`; a busca da chave com as dicas), o `RPathHandle` (0xd4 bytes: o
  acelerador que anda para o desejado depois da espera, o caminho seguinte no fim, o mestre) e a lista
  `fgPathHandles` com `AddInstanceList`, `Update`, `Reset`, `CreatePathHandle`, `DestroyPathHandle` e `GetPathHandle`.
  Paris não tem caminhos de instância (`'ps  '`): só os trilhos.
- **A câmera do AutoDrive.** O `ESetAutoDriveCamOn` chama `RPlayerCamera::SetAutoDriveCamera` (o modo 18, tipo 0x200, o
  `CopterAutoDriveCam` do `camera.ini` com um braço) e `SMissionManager::SetAutoDrive(1)`, que troca a configuração do
  `InputConfigManager` para o tipo 1 (o POV): as ações vão para o `CameraInputCallback` e o `AutoDriveCamInputHandler`
  (os giros, o centro, o zoom e o botão do zoom, a troca de braço pela arma do gerente, o giro de `snow1a_mis3`). O
  `UpdateAutoDriveCam` roda o `UpdateADWeaponAnims` (a troca de braço com o slerp rápido), o `UpdateADTargetAngles` (a
  mira automática no alvo do `WTargetPicker`), o `LimitPitchYaw` (as zonas mortas e os limites por braço), o repouso e
  as transições, e o caso 0x200 do `DirectorChangeCameraMode`.
- **As câmeras animadas.** O `EPlayCameraAnim` (63) chama `TriggerAnimationCamera` com um registro `'Cams'` (64 bytes:
  a matriz e +0x1c a animação do `RAnimEngine`): 11 dos 18 de Paris não têm animação, e o `UpdateWorldAnimationCam` leva a
  câmera do olho ao registro pelo `RCameraSpline` (Bézier ou Catmull-Rom, as bases em 0x0037e4a0/0x0037e4e0, o slerp dos
  quatérnios) ao longo da transição; o `EPlayCameraSpline` (204) chama `TriggerAIPathAnimationCamera`, uma câmera num
  `AISplinePath` próprio movido pelo `RPathEngine`. Os outros: 242 `ECameraLockOn` (o ponto, o deslocamento e os quadros),
  246 `ECameraZoom`, 251 `ECameraShake`, 207 `EChangeCarCameraView` (`ForceCameraChange`), 76/77 e 233/234 (o AutoDrive
  ligado e desligado, com e sem filme), o `EEndCameraAnim` e o `SetLastSelectableCameraMode`. Os quadros dos eventos vêm
  em 1/60 s e passam para a taxa da simulação (`(float)(uint)` acima de 1).
- **A troca de carro.** O gatilho 741 (758,9, -1,5, 222,2, raio 80; ignorado, a bandeira 0x2000, até um `EEnableTrigger`) tem a troca:
  `ECameraLockOn`, `ECustomTransition` (240: com "paris", `PBondCar::ChangeCarType` para o `SECONDARY_TYPE`), `ESetVideo`,
  `EEndControlToSpline`, `ESetAutoDriveCamOff`, `EResetPlayerCarPos` (a largada do carro no gatilho 683) e os
  `EDeactivateAIElement`.

### O quadrilátero no céu

O quadrilátero chapado azul e amarelo no céu de Paris era o facho `SpLite04` (a textura `spot`, de alfa até 0x1b)
desenhado opaco: o começo do `EAGL::TAR` (0x0027cc88, sem nome: o 0x0027e1e8 que anexa a imagem o chama antes do
0x0027cd68) não estava portado e o TCC ficava 0 em toda textura do mundo e dos carros (o alfa só do vértice). Com o TCC
o facho fica quase transparente, e os vidros e os faróis do Vanquish também.

### Como a seção anda em Paris

Na dificuldade normal e na difícil a missão falha aos 15,3 s: o gatilho 644 (o passo 918) chama a etapa 42, cuja regra
de dificuldade (normal ou difícil) roda `EKillAIElement`, `EPlayerLose` e `EDisableTrigger`. No jogo os eventos da
regra 1 da etapa 19 (o pneu furado, a propriedade `BLOWOUT`, do primeiro inimigo, que o jogador acerta do helicóptero:
três `EDisableTrigger`) devem desligá-lo antes; sem a IA e sem as armas o port não chega lá. Na fácil (`--difficulty 0`) a seção segue:
a câmera animada do mundo no gatilho 712 (o passo 969, com as tarjas e o AutoDrive desligado para o filme), a etapa 5,
o acelerador do trilho pelos gatilhos 698, 700, 735 e 703, a etapa 8 (o passo 2725), que espera o segundo inimigo
levar dano (a regra 2) ou ficar com os pneus furados ou vazio (o cronômetro 5 da etapa leva ao `EPlayerLose`), e a
câmera do trilho no gatilho 569 (o passo 3035), que fica parada (a mesma imagem aos 52 e aos 58 s) até a IA andar. Num experimento com o dano forçado as regras seguem (a etapa 21, o caminho
seguinte, o trilho para no passo 3506 com o acelerador desejado em 0) e esperam mais IA. Para ver a troca:
`--fire-trigger 741@20` (o carro vira o Vanquish na largada do 683, o objetivo 2801, o cronômetro 3 de 300 s, a câmera
Heli de perseguição).

### A conferência (a parte 19 do `nf_driving_check`, `--so helicoptero`)

O código original roda no interpretador MIPS com a VU0, ao lado do porte (`tests/driving_check_heli.cpp`,
`driving_check_heli_rail.cpp`, `driving_check_heli_cam.cpp`), com os 19 `'pt'` e os 19 `'AISp'` de Paris, 14 caminhos
montados à mão (os canais em Bézier, escalares e em degrau que Paris não tem) e valores sorteados (`--casos 4000`):

- o `PathInfo` (`GetKnotIndex` 4.000, `EvaluateLinear` 3.605, `EvaluateSpline` 395, `Evaluate`, `EvaluateMatrix` e
  `FindChannelIndex` 4.000 cada), `GetApplyTransform` e `RotateSplineAboutBase` (1.000 cada), o `RPathHandle`
  (construtor e `Init` 759, `Update` 15.092 com o acelerador, a espera, o fim, o seguinte e o mestre), o `RPathEngine`
  (`AddInstanceList` 20, `Update` 600, `Reset` 20, `GetPathHandle` 162, `CreatePathHandle` 20), o `AISplinePath` (20
  de cada) e `SetSplinePath`/`SetNextSplinePath` (80): 0 diferentes;
- o trilho com o Vanquish do porte (a classe 0 e o seu `AIGroundVehicle` registrado pelo `AIVehicleController`) e a
  cópia do mundo de Paris, em 500 cenas: os destrutores 202 (752 casos), 239 (556), 209 (313), 108 (264), 116 (233), 153
  (261), 215 (256), 42 (99) e 43 (101) pelas tabelas virtuais do PS2 (`EngageSplinePath`, `SetCarClass`,
  `InitializeCarControls`, `DisengageSplinePath`, `EngageAutoDrive`, `DisengageAutoDrive`, `SetTargetGas`,
  `SetTargetBrake`, `SetAttackMode`, `SetNavigateMode` e `SetControlToCPU` originais; o `WRoadNav::InitAtPoint` do 42,
  conferido na parte 14, anotado e comparado com o corpo do porte), o `RPathEngine::Update` (10.410 quadros), o
  `DoSplinePathUpdate` (5.504) e o `ProcessSplinePhysics` (6.522, 48 com `IS_SNOWMOBILE` e 27 com `SUB_PHYSICS`
  sorteados): 0 diferentes;
- a câmera: o `LoadFile` do `helicopter` com o `vanquish` (22 modos: AutoDrive 18, WorldAnim 14, AIPathAnim 16; os
  braços, as tabelas Heli), o construtor e 2 x 1.000 quadros de `UpdateCamera` (uma passada com o braço do disco, outra
  com dois braços sorteados com a zona morta, o repouso, a troca de transformação e o braço preso ao carro) com pedidos
  sorteados entre eles (`SetAutoDriveCamera`, as entradas do POV, `CameraLockOn`, `WeaponFired`, o alvo,
  `TriggerAnimationCamera`, `TriggerAIPathAnimationCamera` com o `RPathEngine` andando, `EndCameraAnim`,
  `SetLastSelectableCameraMode`, `ForceCameraChange`, `ShakeCamera`, as pausas) e os destrutores 63, 204, 242, 246,
  251 e 207 (51 casos); compara a câmera inteira (0x370 bytes), o `RPlayerCamState`, os globais, os braços, a fila do
  diretor, o `RCameraSpline` e os registros `'Cams'`: 0 diferentes.

Ela achou três erros do port: a fila do diretor (`ProcessDirectorLogic`) descartava os pedidos com a bandeira 4 (caem
os sem ela, se não são os últimos); os valores iniciais do `.data` de `fAutoDrivePrevWeapQuat` (0, 0, 0, 1) e
`fAutoDriveWeapInterpolTime` (1); e o `SetNavigateMode(1)` do `AIGroundVehicle` só zerava o ponteiro do trilho (o
`RPathHandle` ficava na lista). Os pedidos por id da câmera só vão para os modos Heli (os modos Bumper e Dashboard da
tabela não foram portados). Os testes do `nf_tests` (`tests/test_driving_heli.cpp`, sem dados do jogo): as chaves e o
meio de um canal linear, o slerp de um canal de rotação na esfera e o trilho de um carro pelos eventos 202 (sem caminho,
com um elemento da IA e com o jogador), 116, 239 e 43.

### No `nf_driving`

`--drive` sem `--fire-trigger` joga a seção: o trilho, a câmera do AutoDrive com o teclado e o controle (a configuração
do POV do `InputConfigManager`), as câmeras animadas e os eventos. O relatório ganha as linhas `camera_modos` (o tipo e o id do
modo, os quadros do AutoDrive, o braço, a inclinação e o rumo em graus, o ponto preso, os quadros das câmeras animadas,
as ações, a configuração da entrada, os eventos de câmera) e `trilho` (o estado do `AIGroundVehicle`, o `RPathHandle`:
o tempo, o acelerador, o desejado; os engates, os passos, a física, os eventos de caminho, as trocas de carro).
`NF_DRV_PICK` e `NF_DRV_HIDE` (depuração) acham e escondem instâncias da pista. As capturas (`out/agent_drv_heli/` do
checkout principal, fora do git, 1280x720): `e3.png` (a largada: o AutoDrive olhando o cais a -60 graus), `e17.png` (a
câmera animada do mundo do gatilho 712, com as tarjas), `e30.png` (o AutoDrive de volta, -35 graus), `e52.png` (a câmera
do trilho do gatilho 569, parada), `s20.png` ("Mission Failed" aos 15,3 s na normal),
`h21.png` e `h30.png` (a troca pelo gatilho 741: o Vanquish na seção do carro).

### O custo

O quadro inteiro (`--bench-frames 600 --adapter integrated`, 1080p, a Radeon integrada, junto com o executável da
main): a seção do helicóptero 3,39/3,42 ms (295 quadros por segundo; na main o carro ficava parado na origem: 5,09/5,12
ms, outra vista); a largada de sempre sem a missão (`--car-pos -327.7,20,-17.7,0.3155`) 7,85/7,82 ms contra 6,75/6,76
ms, porque um gatilho da rua roda agora o `EChangeCarCameraView` e o `ECameraZoom` (o modo 7 com 16 graus: o carro
enche a tela, e os métodos do carro custam mais por pixel); a 640x360 a mesma cena custa menos que na main (1,87 contra
2,02 ms na Radeon, 0,73 contra 0,91 ms na RTX 5090).

### O que falta e a estimativa

A seção em si está feita; falta o que ela pede de fora: os inimigos (o `AIElementController`, os caminhões e os carros
que o jogador acerta do helicóptero, as propriedades `BLOWOUT`/`EMPTY` e o dano das regras) e as armas do AutoDrive
(o fuzil do braço, o `SWeaponManager`), contados nas linhas da IA e das armas da seção 19. Da câmera: os 7 registros
`'Cams'` com animação do `RAnimEngine`, o `EPlayCarCameraAnim` (97), os modos Bumper, Dashboard, Spline, Ellipse, Fixed,
Tumble e o `RelativeAnim`, o `SetOldCameraMode` com transição (que monta uma câmera na pilha), a câmera do míssil; do
mundo: os gatilhos tocados pelas instâncias dos trilhos (`kTrigInstances`, os 544 `EWakeupSmackable` de Paris) no
`WTriggerManager::Update`. A linha "a seção do helicóptero" (2-4 dias) sai da conta da seção 19 e entram 1-2 dias para
estas câmeras: Paris fica em **12-19 dias** (eram 13-21) e as fases 2-4 em **12-20 dias** (eram 13-22). O
`DoAutoDriveUpdate` (o estado 2 do `AIGroundVehicle`, a direção automática pela rede depois do 42) fica com a IA.

## 24. A fase 3: as armas ligadas à IA (a mira, a trava nos alvos, as balas, os atores, a direção automática)

Até a seção 23 as armas, a IA e os personagens de Paris andavam cada um por si: a missão só se ganhava e se perdia com
substitutos (`--fire-trigger`, `--kill-element`, `--emp-hit`, `--damage-hit`). Esta seção liga as peças pelo código do
PS2: a mira pela câmera do jogador, a trava nos alvos (`WTargetPicker`), as balas da metralhadora e do fuzil
(`RayShell`, `Shell`, as janelas), o `EFireWeapon` inteiro, o Q-Pulse que para a van pelo `PBondCar::AttackWithEmp`, a
direção automática do `AIGroundVehicle` (o estado 2), as armas dos motoristas da IA, e a camada dos atores com as mãos
do Bond (o fuzil de precisão da seção do helicóptero). Tudo do MIPS e da VU0 do PS2, conferido contra o código original
no interpretador (a parte 20 do `nf_driving_check`, `--so integracao`). O nightfire-cxbx (NightfireResearch) deu os
nomes e o leiaute de várias classes (`WTargetPicker`, `WTargetable`, `AICharacter*`, `Act*`); a leitura é toda do PS2.
Paris agora se joga do começo ao fim sem substitutos: o atirador e o piloto de depuração só apertam os botões do
controle do PS2.

### A mira pela câmera (`camera/camera_aim.h`)

`RPlayerCamera::GetForwardAimVec4` (0x001bcca0), `GetAimMatrix4` (0x001bcd10), `CameraAiming` (0x001bd2f8) e
`RCamera::CreateMatrix4Inv` (0x001a4468): a frente e a matriz da mira guardadas pela câmera (+0x170, +0x180), a
mistura com a linha 2 da matriz da câmera pelo `t` (o w fica: a máscara xyz da VU0), a rotação pelos eixos do carro
(`BuildRotationMat4` e `VU0_MATRIX4_mult`) e o zoom da mira do `RPlayerCamState`. `current_player_camera()` é o
ponteiro que o PS2 lê do diretor.

### A trava nos alvos (`game/target_picker.h`)

O `WTargetPicker` do global 0x33a970 (0xb0 bytes, o leiaute do PS2) e os `WTargetable` (0x50: o ponto, o corpo, o
`AICharacter` e o `AIVehicle`): o construtor, `RegisterTarget`/`UnregisterTarget`/`ClearAllTargets`, `UpdateTargets`
(a cada quadro desenhado, pelo `RRenderWorldCamera::DrawStaticWorldGeometry`), `UpdateSelection` (o mais perto do
centro dentro do raio de busca, 130 px; a trava em 0,25 s; solta além de 145 px), `MoveTargetingCursors`,
`IsPointOnScreen`, o ponto na tela (0x0022a128) e fora dela (0x0022a430) pela câmera e pelo `fgRenderer` (640x448,
widescreen e o multiplicador do fov), `Activate/DeactivateTargeting`, `GetSelectedTarget`, `CycleToNextTarget`,
`UpdateAutoDriveTargeting` (o quatérnio da mira do AutoDrive com o slerp rápido), os dois `GetDirectionToTarget`, os
comparadores do qsort; dos `WTargetable`: os quatro construtores, `CommonInit`, `UpdatePosition`, `UpdateVisibility`
(até 240 m e o mundo não corta: `WCollisionMgr::StepCheckHitWorld`, passos de 48 m), as distâncias; `WSimpleZone`
(`SetPosition`, `IsZoneInRange`) e o `TransformPoint` xyz (0x002d18e8, agora o único: o `RPathEngine` usa este). Os
carros da IA de classe 2 se registram na construção do `PBondCar` (o farol de alvo, `target_beacon`) e saem no
destrutor; o míssil (seção 22) usa o mesmo picker. A trava só pega no modo 1 com a arma 0x1c: o fuzil de precisão (0x1a)
da seção do helicóptero não trava, a mira é a do jogador.

### As balas (`game/shells.h`, `rayshell.cpp`, `shell.cpp`, `physics/world_window.cpp`)

O `EFireWeapon` (o evento 10, `game/fire_weapon.cpp`) agora é o destrutor inteiro: as balas das metralhadoras e do
fuzil, a pontaria da IA (o `GetEnemyAccuracyBoost` da missão, a perturbação gaussiana `Util_PerturbVector` com o
`Util_GaussRandom` sobre o `rand` e o `__ieee754_log` da biblioteca do PS2), o `ActWeapon` dos personagens e a mira
pela câmera. O `RayShell` (0xd0 bytes) faz o tiro inteiro no construtor: o raio do cano até o alcance (220 m; 400 no
fuzil 0x1a) contra os corpos da `Simulation` (`CollideWithObjects`: as esferas, os personagens como esferas esticadas,
as caixas; `ConsiderVictim` guarda o mais perto e o segundo) e contra o mundo em passos (`CollideWithWorld`:
`CheckHitWorld`, as janelas, os efeitos e o dano do mundo); sem mundo no caminho, `Fire` e `DamagePhysicsObject` levam o
dano ao dono (o `ApplyDamage` pela tabela virtual, o tipo 2: o tiro). Cada tiro guarda o rastro (64 vagas), o clarão
(8) e o raio para os gatilhos (63). O `Shell` (0x90) é o projétil lento do canhão 0x10: um corpo simples de tipo 5,
`Simulate` a cada passo com o `DetectCollisionsSRB`, `ApplyWorldEffects` (o efeito pelo material, as faíscas, a zona da
IA, o som) e `ApplyWorldDamage` (a queimadura, os estímulos 0x13..0x16 da instância atingida).
`WCollisionMgr::CheckHitWindow` (0x002153b0, com 0x0025b890 e 0x0025b9f8) acha as 53.446 janelas de Paris e quebra a
atingida (o `EHitWindow` fica anotado). O `WTriggerManager::Update` limpa os raios ativos no fim, como o PS2
(`ClearActiveRayShells`); o caminho dos raios nos gatilhos não é portado (contado: `raios_sem_conferencia`).

### A direção automática e as armas dos motoristas (`ai/ground_vehicle_agent.cpp`, `ai/ai_weapons.h`)

O estado 2 do `AIGroundVehicle` (`DoAutoDriveUpdate`, 0x00131080; o `Update` chama `UpdateStuck`, ele,
`DoAttackMode` e `DoMoveMode`): a cada quarto de segundo o vetor dirigível até o ponto da rua do navegador, o
`InitAtPoint` quando perdido, o navegador adiantado 7,5 m, a troca de faixa e o `ResetCar` no ponto do navegador quando
preso. As armas: `ClearShot` (0x0012f530, nenhum agente amigo na linha de tiro), `TurnTurret` (0x0012f170, a torre pelo
ângulo da busca com o `TURRET_RANGE` dos atributos, o quadro da animação pelo `RAnimEngine::Handle::SetFrame`),
`FireBullets` (0x0012f730), `FireRockets` (0x0012fa28) e `FireMissiles` (0x0012fd58): o cone (0,99, 0,9, 0,707), o
cronômetro de cada arma, os sistemas de animação e o estímulo 10 que vira o `EFireWeapon`. O `DoWeaponUpdate` chama as
funções reais (a parte 17 troca por anotações). `RAnimEngine::Handle::SetFrame` (0x001a2630), o do sistema (0x001a38e0)
e o `SetEventDynamicData` (0x001a2700) entraram em `render/anim_engine`. Os carros da IA de Paris não têm
metralhadora, foguetes, mísseis nem torre: os tiros dos inimigos de Paris vêm dos passageiros (abaixo, o que falta).

### Os atores, os personagens e as mãos do Bond (`anim/act_actor.h`, `game/ai_character.h`)

A camada dos atores do `DRIVING.ELF` (`ActActorDatabase.obj`, 70 funções): o `ActManager` (os bancos dos esqueletos, dos
modelos, das texturas, das armas, das animações e dos eventos), o `ActActorDatabase`, o `ActActor` (as matrizes, o
poser, o `StandardAnimationController`, as armas nos ossos, `Fire`, `Update`), o `ActCharacter` e o
`ActCharacterInfo`, o `ActWeapon` (um `RSceneObj` com o `RAnimEngine::Handle` do CARP da arma, por onde o tiro passa),
os eventos das animações (`ActEventResolver`, `ActEvents`, `EventTarget::ResolveEventId`, `RawEventChannel::Eval`,
`FnCompoundChannel::EvalEvent`) e o resto do `ActPoser` (o modo 1 do `DoMainPose`/`DoEventPose`, as rotações, a
correção da matriz pelo chão). Como um personagem atira no PS2: a chave "Fire" da animação de eventos chega ao
`ActActor::Fire`, que pede o estímulo 10 à arma; a chave da animação da arma cria o `EFireWeapon` com o `ActWeapon` no
`EventDynamicData`. Sobre ela, o `AICharacter` (a máquina de estados pela tabela virtual do PS2, a zona do dano, o
movimento; `Execute`, `NotifyZoneDamage`, `MoveToPoint`), o `AICharacterEnemy` (`TargetIsInRange`, `NeedsToReload`,
`TargetIsAcquired`), o `Human` (o `PhysicsObject` de tipo 4 com um corpo simples de tipo 2 que leva o dano dos tiros ao
personagem: `Simulation::SpawnHumanObject`, `ApplyDamage`, `Simulate`) e as mãos do Bond (`AICharacterHands`: o braço com
a arma na câmera do AutoDrive; os estados, `Fire`, `Reload`, `ChangeWeapon`, `UpdateRotPos`, `GetRotPos`,
`GetAimFraction`). Com a direção automática o `SWeaponManager::FireWeapon` pergunta `AbleToFire` e chama `Fire`: o
fuzil do braço atira pela animação. O `nf_driving` desenha as mãos pela família de pele (`drive_actors`).

### O Q-Pulse

O míssil 0xe (seção 22) no carro: `PhysicsObject::AttackWithEmp` pela tabela virtual (+0x254), que o `BondCar` leva ao
`PBondCar::AttackWithEmp` (seção 21). O L1 do controle é a arma especial (`GAMEACTION_FIREGADGET`).

### As definições duplicadas

Cada endereço `// [driving] 0x...` tem uma só definição (0 duplicados em `src/driving` e `src/driving_app`, com os
efeitos da seção 25): o `OrthoInverse` (0x002d19b0), o `AdvanceFilePtr` (0x001ffc40), o `BuildRotate`, o
`DeleteSmackableObject` (0x002019a0), o `PhysicsObject::GetSig` (0x001958a8), o construtor do `PhysicsObject`
(0x00195470), o `RegisterZone` (0x001402f8), o `ApplyDamage` (0x00189598), o `TransformPoint` (0x002d18e8), o
`WTargetable::WTargetable(float, float, float)` (0x00229490) e o `GetAimMatrix4` (0x001bcd10) ficaram com uma definição;
as partes de uma função dividida em dois arquivos citam a marca da outra.

### A conferência (a parte 20 do `nf_driving_check`, `--so integracao`)

`--so integracao.tiros`, `.atores` e `.ia` rodam um pedaço. Com `--casos 4000` (o original no interpretador MIPS com a
VU0, o porte ao lado; Paris com 600 pontos de chão):

| Caso | Casos | Diferentes |
|---|---|---|
| as utilidades das balas (`Util_CollideRayWithSphere`/`StretchedSphere`, `GenerateMatrixUnit`, os dois `PerturbVector`, o `__ieee754_log`, `GetEnemyAccuracyBoost`) | 7 x 4.000 | 0 |
| os globais do `RayShell` (`StaticStoreTracer`, `DeleteTracer`, `ClearTracers`, `Reset`, `AddFlash`, `AddNewRayShellForTriggerCheck`, os `Get*`, os `Clear*`) | ~490-513 cada | 0 |
| o `RayShell` inteiro (2.246 acertos em corpos, 1.831 no mundo, 100 janelas, 197 explosões) | 4.000 | 0 |
| `SpawnShellObject`, os passos do `Shell::Simulate`, `ApplyWorldEffects`, `ApplyWorldDamage` | 1.000 + 31.168 + 2.000 + 1.197 | 0 |
| `WCollisionMgr::CheckHitWindow` em Paris (1.950 janelas atingidas) | 4.000 | 0 |
| o `EFireWeapon` (1.722 `RayShell`, 505 `Shell`, 1.285 mísseis, 1.206 `ActWeapon`, 675 carros da IA, 192 helicópteros, 639 pela câmera) | 4.000 | 0 |
| o `AICharacter`, o `AICharacterHands` (construtores, `Execute`, `NotifyZoneDamage`, `MoveToPoint`, `UpdateRotPos`, `GetRotPos`, `GetAimFraction`), `Util_VecToPoleDepthAndLong`, `Util_CharMapLookup`, o `AICharacterEnemy`, o `Human` | 4.000 cada | 0 |
| as mãos: `AbleToFire`, `Fire`, `Reload`, `ChangeWeapon`, `ShowHands`, `HideHands`, os estados | 549-606 cada | 0 |
| o `ActPoser` (`Rotate`, `RotateX`, `CalculateFinalCorrectionMatrix`, `SnapMatrixLevelWithGround`); `RawEventChannel::Eval`, `ResolveEventId` | 960-1.036; 4.000, 3.000 | 0 |
| a mira pela câmera (`GetForwardAimVec4`, `GetAimMatrix4`, `CameraAiming`, `CreateMatrix4Inv`), `TransformPoint`, `WSimpleZone` | 4.000 cada | 0 |
| o `WTargetPicker` (a tela, os comparadores, `StepCheckHitWorld` e `UpdateVisibility` em Paris: 4.000 cada; os construtores 1.282; as chamadas sorteadas 2.294; `UpdateTargets` 827; `ATargeting::SetState` 2.194) | | 0 |
| o estado 2 do `AIGroundVehicle` (`Update` 2.909, `DoAutoDriveUpdate` 1.892; 5 casos fora: o original lê memória inválida ou lixo da pilha) | | 0 |
| as armas dos motoristas (`FireBullets` 314, `FireRockets` 340, `FireMissiles` 343, `TurnTurret` 304, o `DoWeaponUpdate` inteiro 336, `ClearShot` 363; 124 estímulos de tiro do original) | | 0 |

A conferência achou erros de leitura do porte: o passo do navegador da direção automática é 7,5 m (não 7); as anotações
do `ApplyDamage` levam o índice do `GetOrderedBody`; o `Missile::CheckForCollision` relê o míssil guiado do gerente a
cada vez (a parte 18 dava 3 diferentes). O `nf_driving_check . --casos 4000 --quadros 600` inteiro: 0 diferentes em
todas as partes (com a 21 dos efeitos).

### Paris jogada

Três ajudas de depuração do `nf_driving` (só do port: o jogo não sabe delas, só vê os botões do controle do PS2):

- `--gunner e1[,e2...][@altura]` (com `--gunner-tyres`): o atirador da seção do helicóptero. O direcional esquerdo gira
  a câmera do AutoDrive até a frente da mira (`GetForwardAimVec4`) apontar para o carro do primeiro elemento da lista
  acordado, vivo, sem a propriedade `BLOWOUT` nem a bandeira "pode estar morto"; o R1 atira quando o erro fica abaixo de
  0,6 grau (`--gunner-limit graus`, desde a seção 26; `NF_DRV_GUN_LIMIT` o troca). O erro é medido no referencial da mira (numa câmera animada a vista olha para outro
  lado); `--gunner-tyres` mira no pneu mais perto da câmera (a alavanca 0-3 do corpo), o que a regra `BLOWOUT` pede
  (um pneu estourado com os passageiros vivos). `NF_DRV_GUN_LOG=1` dá o erro, o modo e o rumo da câmera a cada meio
  segundo.
- `--autopilot e@x,z[,v];...` (seção 21; agora com a velocidade por ponto e os pontos passados ficando para trás) e
  `--autopilot-fire`: seguindo o elemento, o L1 (o Q-Pulse) com o veículo a menos de 50 m e 0,15 rad da frente. Com o
  atirador, o piloto só dirige depois da troca do helicóptero pelo carro (`ECustomTransition`).
- `--drive-input` aceita os direcionais (`LU/LD`, `RL/RR`, `RU/RD`).

Os finais (capturas em `out/agent_drv_integ/shots/` do checkout principal, fora do git, 1280x720). `ROTA` são os 40
pontos (x, z, velocidade) que o piloto segue do ponto do 683 até a ponte, onde acha a van:

```
-321,20,40;-322,-100,40;-322,-200,40;-325,-300,30;-340,-330,25;-363,-344,28;-411,-346,40;-470,-342,40;-506,-339,30;-535,-345,28;-560,-353,28;-580,-360,28;-603,-381,35;-650,-428,40;-690,-468,40;-745,-523,35;-769,-547,28;-785,-575,30;-786,-660,40;-790,-740,35;-853,-800,35;-890,-837,30;-891,-964,40;-898,-1000,35;-910,-1040,35;-925,-1064,35;-942,-1080,35;-978,-1117,40;-1000,-1140,40;-1047,-1197,35;-1068,-1210,28;-1083,-1221,28;-1100,-1236,30;-1125,-1257,30;-1138,-1270,28;-1143,-1295,30;-1143,-1324,35;-1142.7,-1407,40;-1130,-1440,35;-1132,-1615,40
```


- **"Mission Accomplished"** (`paris_win.png`, o estado 3 na etapa 2 aos 206 s): `--gunner 3,11,4,13,6,7,5,8
  --gunner-tyres --autopilot "1@ROTA" --autopilot-fire --drive-input none:210`. Na seção do helicóptero o fuzil das mãos
  estoura os pneus dos caminhões da lista (5 pneus em 38 tiros); o 6 precisa estourar entre o fim da câmera do
  caminho da IA (a `ESetAutoDriveCamOnMovie` do gatilho 566) e o gatilho 720 que começa a cena da falha: ele só entra no
  limite de giro do AutoDrive (`LimitPitchYaw`, o rumo a -113 graus) pouco mais de um segundo antes. Aos 85 s o 727 passa
  ao carro (o objetivo 2801); o piloto segue a rota, acha a van, o Q-Pulse a para (`AttackWithEmp`, 1 acerto em 9
  disparos) e a regra ganha. **Desde a seção 26 este comando perde na etapa 12** (os raios do fuzil acordam dois postes
  do cais, ver lá); o que ganha hoje: `--gunner-limit 0.9 --gunner 3,11,4,13,6,7,5,8@0.15 --gunner-tyres --autopilot
  "1@ROTA" --autopilot-fire --drive-input none:260`.
- **"Mission Failed", a van foge** (`paris_lose_van.png`, o estado 4 na etapa 29 aos 260 s): o mesmo sem
  `--autopilot-fire`. Hoje: o comando que ganha, acima, sem `--autopilot-fire` (o estado 4 na etapa 29 no fim dos
  260 s; `paris_perde_van.png` da seção 26).
- **"Mission Failed", na seção do helicóptero** (`paris_lose_heli.png`, o estado 4 na etapa 12; a falha perto dos
  74 s): `--gunner 3,11,4,13,6,7,5,8 --drive-input none:100`, mirando no centro dos carros: o caminhão 6 sai inteiro, o
  carro do elemento 9 chega ao gatilho 627 (que o `BLOWOUT` do 6 teria desligado), o `EKillAIElement` o mata e a regra
  Death perde. Hoje também o comando do "Mission Accomplished" acima, com o limite de 0,6 grau (`paris_perde_heli.png`
  da seção 26).
- `paris_heli_aim.png`: as mãos do Bond com o fuzil na seção do helicóptero aos 76 s.

**"Mission Failed" pelo carro destruído pelos tiros inimigos não se alcança** (alcançado na seção 26): em Paris quem atira no jogador são os
passageiros (17 dos 25 elementos levam um: os 14 `paris_henchbigbox` e os três `paradis_car`), `AICharacterEnemyWindow`,
`AICharacterEnemySunroof` e `AICharacterEnemyDriver` (cerca de 13 KB de MIPS, as classes de `AICharacterEnemyGround.obj`
e `AICharacterBond.obj`), que não foram portados; os carros da IA não têm armas próprias. Sem eles o carro do jogador só
perde vida em batidas e explosões (2.625 -> 2.024 na partida ganha). O final pela regra Death continua alcançável com
`--damage-hit` (seção 21).

### Divergências

- O som do personagem (`AICharacter::GetACharacter`, +0x94), a câmera das mãos (`UpdateCamera`, `AimZoom`,
  `AimRelease` por ganchos do `nf_driving`), o `AMenuSound` do ferrolho e os bancos de modelos e texturas dos atores
  (o `CarModel` do port) estão no cabeçalho de `game/ai_character.h` e `anim/act_actor.h`.
- O caminho dos raios nos gatilhos (`WTriggerManager::Process(int)` 0x0022def0) não era portado: contado (portado na
  seção 26).
- O `EHitWindow` (as janelas que quebram) e os efeitos das balas (a galeria por material, as faíscas, a queimadura) ficam
  anotados.
- As leituras de memória inválida do original (`PhysicsObject::IsOwnedBy` com o objeto nulo, o tipo do primeiro
  atingido sem conferir a assinatura) são guardadas e contadas.

### O custo

Não medido (o computador estava em uso: nenhuma janela).

### O que falta e a estimativa

Para Paris inteira: os passageiros que atiram (`AICharacterEnemyWindow/Sunroof/Driver`, a criação pelo
`AIElementController::WakeUp`, o desenho pela camada dos atores; 1,5-2,5 dias, com a camada pronta), a passageira e o
Bond no carro (`AICharacterPassenger`, `AICharacterBond`), o caminho dos raios nos gatilhos (0,5 dia), o resto da câmera
da seção do helicóptero (seção 23: 1-2 dias), os gatilhos tocados pelas instâncias dos trilhos, o resto do desenho dos
efeitos (seção 25: 1-2 dias, com os efeitos das balas e o `EHitWindow`), as falas e a música, os filmes e o HUD de
verdade. Paris não tem helicópteros inimigos (os 25 elementos são carros de tipo 1): a linha "os personagens e os
helicópteros inimigos" da seção 21 (2-3 dias) fica nos passageiros. Saem as balas da metralhadora e a trava nos alvos
(0,5-1 dia) e a direção automática; sobre as contas da seção 25 (Paris em 8-14 dias, as fases 2-4 em 8-15), Paris fica
em **7-12 dias** e as fases 2-4 em **7-13 dias**.

## 25. A fase 3: os efeitos de Paris desenhados (a galeria, as partículas, as luzes, as marcas, o rastro dos mísseis)

Até a seção 22 as explosões e os eventos de efeito só eram anotados: o GFX::Trigger, as luzes e as marcas no chão não
existiam. Esta seção porta, do MIPS do PS2, o que desenha os efeitos de Paris: a galeria dos efeitos (o "Giotto" da EA),
o sistema de partículas, as luzes pontuais das explosões e dos mísseis, as marcas de queimado, o rastro dos mísseis e os
eventos de efeito que a missão cria; e os dois render methods que eles usam, lidos do microprograma da VU1 e conferidos
no `--verify`. Os nomes são do nightfire-cxbx (NightfireResearch) e do `DRIVING.SYM`; a verdade é o PS2.

### Como o PS2 faz

- **A galeria** (`GFX`, `GFXGallery`; o `data\gfx\data\gall1.gal` do `MISC.VIV`, um `GGAL` com 106 efeitos `GANM`):
  cada efeito é um grupo de trilhas com chaves no tempo: `gp` (partículas: um `ParticleData` de 0xb0 bytes por chave),
  `gs` (sons), `gg` (clarões), `gd` (destroços). O `GFX::Trigger(ref, pos, quat, pos2, quat2, cena, instância, extra)`
  põe o efeito numa das 512 telas (`GfxCanvas`, 0x90 bytes; as bandeiras do efeito: repetir, a velocidade do objeto);
  presa a um objeto de cena, a tela segue a instância (`RSceneObj::GetInstancePosition` e a matriz dela vezes a
  posição e o quatérnio). A cada quadro desenhado o `GFX::Update` (`FRAME_Draw`) anda as telas vivas: o
  `CANVAS_Draw` interpola as chaves de cada trilha, o `PARTICLE_Draw` cria (na primeira vez, `PARTICLE_Add`: o tipo na
  biblioteca, a gravidade e o giro em unidades do jogo) e atualiza o emissor da trilha, e as telas paradas voltam para a
  lista livre. O recorte (`CULL_GetNextCanvas`/`CULL_SetCanvas`, no fim do `PerformCulling`) esconde as telas fora da
  vista.
- **As partículas** (`RParticleSystemManager`, `RParticleLibrary`, `RParticleSystem`, `RParticleParticleCache`): a
  biblioteca guarda até 256 tipos (o `'dflt'` do `.data` e os da galeria, pelo `MungeData`); um sistema é um emissor com
  o tipo, a taxa e o tempo; o `UpdateSpawnAllSystems` (até 3 tiques por quadro) cria as partículas novas
  (`ResetParticle`) no cache de quadro duplo (2 x 1024 partículas de 0x20 bytes) e o `UpdateAndRenderAllSystems` anda
  e desenha o cache em blocos de 512: o `Upload` copia o bloco para o scratchpad, o `UpdateBlock` move as partículas
  (velocidade, gravidade, arrasto, vida; as grudadas a um corpo ou a outro efeito) e guarda as vivas para o quadro
  seguinte, o `RenderBlock` monta três listas: os retângulos normais (`RenderSpritesRot`, mistura (Cs - Cd) As + Cd),
  os aditivos (Cs As + Cd) e os riscos (três vértices por partícula, `MatDebugTexture`, Cs FIX + Cd), com as texturas
  do contexto `partic01.ssh` ('MAIN') e o 'sprk' do `common.ssh`.
- **As luzes** (o `RHighLevelLightManager` em `RLightManager + 0x2e0`): a explosão pede `AddExplosion(pos, dano)` (o tipo
  pelo dano: >800, >400, >200, >100), o míssil `AddMissile(pos, tipo, valor)` a cada quadro; no fim de cada quadro o
  `FinishLights` chama o `Process` (cada explosão com a cor da tabela 0x333878 vezes min(intensidade x (2 / idade -
  0,02), 16) e o K do tipo, até apagar; cada míssil com a cor 0x3338f0 e o K vezes max(valor, 1)) e publica o bloco de
  quatro luzes pontuais (`GAME::PositionalLights`, a VU 0x40-0x47) que os métodos do carro, do mundo dinâmico e dos
  personagens somam por vértice (255 - d² K) no quadro seguinte.
- **As marcas** (`RDecalManager`, 96 marcas num anel; o `data\render\decal.dat` da missão, 8 tipos): a explosão chama o
  `RScorchWorld::ScorchGround(pos, tipo 5, tamanho 8)`: a face sob o ponto, recusada se é água (tipo 6), de bandeira 2,
  de instância marcada, inclinada (y da normal < 0,95), ou se os quatro pontos a meio lado não têm face ou estão a mais
  de 0,15 de altura do centro (salvo o tipo 4), ou o tipo de face 9; a marca fica 0,025 acima da face. O `Draw` conta o
  atraso do tipo (5 quadros no 5) e põe o alfa do tipo (0x28).
- **O rastro dos mísseis** (`RMissileStreak`, `Missile::Render`): a cada quadro desenhado o míssil acende a luz e anda o
  rastro (um par de pontos a cada dois quadros, com um tremor do `RRandom` e aberto pela direita da câmera), desenhado
  como faixa de triângulos dos últimos 40 pontos, com a cauda transparente; o EMP (arma 0xe) só acende uma luz azul. O
  míssil do Vanquish (arma 2) e a 0x1e não desenham rastro (como o PS2).
- **Os eventos**: 9 `EPlayEffect` (o efeito de um registro do CARP, solto no mundo pela matriz do evento ou preso a um
  objeto de cena, espelhado quando a bandeira 0x200 pede outra instância), 250 `EPlayGiottoEffect` (pelo nome), 196
  `ENuclearBlast` (o `SWeaponManager::StartNuclearSequence`), 257 `ESetCullDistanceFactor` (o alcance do corte = fator x
  400).

### Os arquivos

| Arquivo | O que tem |
|---|---|
| `src/driving/fx/particles.h/.cpp` | o sistema de partículas: a biblioteca, os sistemas, o gerente, o cache (o `UpdateBlock` num scratchpad próprio, como o `Upload` do PS2), as listas do `RenderBlock` |
| `src/driving/fx/gfx_gallery.h/.cpp` | a galeria: `GFX_Init/LookupReference/Trigger/Update`, as telas, as trilhas `gp`, os grudados, o recorte |
| `src/driving/fx/fx_render.h/.cpp` | o desenho do PC (os lotes) e as referências dos dois métodos (`render_sprites_rot_reference`, `mat_debug_texture_reference`) |
| `src/driving/fx/fx_verify.h/.cpp` | a conferência dos dois métodos no `ps2::VuMachine` |
| `src/driving/fx/fx_lights.h/.cpp` | o `RHighLevelLightManager` e o bloco das luzes pontuais do `RLightManager` |
| `src/driving/fx/decals.h/.cpp` | o `RDecalManager` e o `RScorchWorld::ScorchGround` |
| `src/driving/fx/missile_fx.h/.cpp` | o `RMissileStreak` e a parte de efeitos do `Missile::Render` |
| `src/driving/fx/fx_events.h/.cpp` | os eventos 9, 196, 250 e 257 (e a contagem dos outros) |
| `src/driving_app/drv_fx.h/.cpp` | o lado do `nf_driving`: a carga, as ligações, o passo do quadro, os lotes, o `--check` dos efeitos |

`game/explosion` ganhou no `EffectsEnv` o `gfx_trigger`, o `scorch_ground` e o `explosion_light`, e o
`effects_event_execute` passa os eventos de efeito ao `fx_event_execute`; `world_vis` chama o recorte da galeria no fim do
`PerformCulling`. O interpretador MIPS ganhou o PPACH, o PPACB, o PEXEH e o PREVH.

### Os dois render methods

- **RenderSpritesRot** (os retângulos): a VU1 recebe, por retângulo, a coordenada (x, y, z, tamanho), o UVUV (u, v,
  lado, giro) e a cor; gira os cantos por ESIN do giro e do giro + 1,5708, o raio no recorte é Control.y x tamanho / w
  (Control.y = 66 / (fov x margem 4); Control.z = Control.y x 1,4e7), e o ADC (w = 2048) quando recortado ou quando
  z x escala.z + tamanho x Control.z >= 0. O PC monta os quatro cantos no espaço de recorte, com o x pela razão de
  aspecto (Hor+).
- **MatDebugTexture** (os riscos, as marcas, o rastro): o vértice pela matriz, o STQ vezes Q, a cor; o recorte da VU1 é
  a divergência de sempre (o Direct3D recorta).

O desenho do PC: um par de shaders (`kFxShader`: o vértice no mundo pela vista e projeção, ou já no recorte; o pixel com
o MODULATE do GS e o alfa / 128), as misturas do GS por lote e o Z do estado.

### A conferência (a parte 21 do `nf_driving_check`, `--so efeitos`)

Com `--casos 4000` (o original no interpretador MIPS com a VU0, o porte ao lado; a galeria do disco: 106 efeitos, 330
tipos de partícula):

| Caso | Casos | Diferentes |
|---|---|---|
| `CalcEmitVal` | 4.000 | 0 |
| `RParticleLibrary`: `Reload`, `AddSystem` (`MungeData`), `FindSystemViaTag`, `MungeDataForPlatform` | 4.000 + 12.000 + 4.000 | 0 |
| `RParticle::ResetParticle` | 4.000 | 0 |
| `RParticleSystem`: o construtor, o `SetSystem`, o `Update` | 17.930 | 0 |
| `RParticleParticleCache`: `JustUpdate` (`UpdateBlock`) e `JustRender` (`RenderBlock`) | 200 + 200 | 0 |
| `RParticleSystemManager`: os sistemas, o cache e as listas por quadro | 823 + 823 | 0 |
| `GFX`: `LookupReference`, `Trigger`, os recortes | 4.883 | 0 |
| `GFXGallery`: as telas, as partículas e as listas por quadro | 1.573 + 1.573 | 0 |
| as luzes: as tabelas do `.data`; `AddExplosion`, `AddMissile`, os dois `AddPositionalLight`, `FinishLights` | 1 + 15.000 | 0 |
| as marcas: `AddDecal` e `Draw` (o `decal.dat` do `MIS01.VIV`) | 8.000 | 0 |
| as marcas: `ScorchGround` sobre um mundo sintético (2.186 marcas postas) | 4.000 | 0 |
| o rastro: as tabelas do `Init`; `Update(COORD4 &)`, `Update(void)`, `Draw` | 1 + 4.000 | 0 |
| `Missile::Render` (a luz e o rastro, mísseis sorteados) | 4.000 | 0 |
| os eventos 9, 250, 196 e 257 | 4 x 4.000 | 0 |

A conferência achou um erro de leitura do porte: a marca fica 0,025 acima da face (0x3ccccccd), não 0,1.

O `nf_driving --data . --check --verify` dispara os 106 efeitos (em grupos de 16, com a galeria e o gerente
recomeçados, porque o cache de tipos da galeria tem 256 lugares) diante de uma câmera e passa pelo microprograma
original (o `bondrm.o`, igual à cópia no `DRIVING.ELF`) uma amostra dos retângulos e dos riscos, mais as marcas do
`decal.dat` e rastros de mísseis: `RenderSpritesRot` 8.000 retângulos (48.000 vértices; 1.164 com o ADC), 0
diferentes; `MatDebugTexture` 2.966 triângulos (182 de marcas, 784 de rastros, 2.000 sorteados; 8.898 vértices), 0
diferentes.

### No `nf_driving`

O `--drive` carrega a galeria, as texturas e o `decal.dat` e, a cada passo desenhado, faz o que o PS2 faz entre o
`StartFrameRender` e o fim do quadro: o `RTimeData::Update`, o `Missile::Render` de cada míssil, o `RDecalManager::Draw`,
o `GFX::Update`, as partículas e o `FinishLights`; o quadro desenha as luzes publicadas no fim do anterior. A linha
`efeitos_desenho` do relatório conta as telas, os sistemas, as partículas, os lotes e, na segunda metade, as marcas, as
luzes vivas e publicadas, os rastros e os eventos. Depuração: `--play-effect NOME@s[@m]` dispara um efeito da galeria e
`--spawn-explosion s[@m[@dano]]` cria um `ESpawnExplosion` (o fogo, a luz, a marca, o tranco, os sons) diante da
câmera. Capturas (`out/agent_drv_fx/shots/` do checkout principal, fora do git, 1280x720): `fx_galeria.png` (o
`Ex_Small` aos 0,3 s), `fx_explosao.png` e `fx_explosao2.png` (uma explosão a 16 e a 20 m: o fogo, a fumaça e o carro
iluminado), `fx_marca.png` (a marca no chão 3 s depois), `fx5.png` (dois mísseis do Vanquish no ar: a luz azul, sem
rastro).

### Divergências

- O `ParticleDensity` (0x425148) é 0x100, o do `FeatureManager` do Xbox (+0x60): o PS2 não tem quem o escreva no código
  lido.
- As trilhas `gs` (sons), `gg` (clarões: `RGlareManager`) e `gd` (destroços: `RDebris`) da galeria, o
  `RSceneObj::TriggerFX` (os efeitos e as luzes dinâmicas das instâncias: `AddDynamicLightEffect`) e o `RColorize` não
  foram portados: contados.
- O PS2 anda os efeitos a cada quadro desenhado; o `nf_driving` a cada passo da simulação (60 por segundo, como o PS2
  a 60 quadros), também nas capturas.
- O `RMissileStreak::Update` num rastro vazio num tique ímpar (o jogo não chega lá) escreveria antes do objeto: o porte
  guarda e conta.

### O custo

Não medido (o quadro inteiro fica para a medida da mescla).

### O que falta e a estimativa

Do desenho dos efeitos: os clarões (`RGlareManager`, as trilhas `gg`), os destroços (`RDebris`, `gd`), os sons da
galeria (`gs`, pelo `ASound`), o `TriggerFX` e as luzes dinâmicas das instâncias, os clarões dos tiros
(`DrawMuzzleFlashes`) e os riscos das balas (`RBulletStreak`), as janelas quebradas, o `RenderAllDeferredEffects`, as
marcas de pneu, o rastro das granadas (`Grenade::Render`), o `ScorchWorld` das balas (`Shell::ApplyWorldDamage`) e o do
radar (`SWeaponManager::UpdateRadarDish`); dos eventos, o 36 (`EPlaySystemAnim`: o `RAnimEngine` do objeto pela IA ou
pelas tabelas de objetos), o 130 (`ESetCollisionGeometry`), o 173 (`ESuppressEffect`, o `TriggerFX`), o 252/253 (os
veículos da IA) e o 279 (o objeto refletido). A linha "o desenho dos efeitos" da seção 22 (2-4 dias) cai para 1-2 dias:
sobre as contas da seção 21 (Paris em 10-16 dias, as fases 2-4 em 10-17), Paris fica em **8-14 dias** e as fases 2-4 em
**8-15 dias** (a seção 24, juntada depois, leva as contas a 7-12 e 7-13).

## 26. A fase 3: os passageiros que atiram (a janela, o teto solar, o motorista) e o que os tiros deles pedem

Até a seção 24 quem atirava em Paris era só o jogador: os carros da IA não têm armas próprias, e quem atira no Vanquish
são os passageiros. Esta seção porta, do MIPS do PS2, as três classes de passageiro que Paris usa, a criação delas pelo
`AIElementController::WakeUp`, o que elas pedem do ator (as sobreposições globais da pose), do carro (o alvo da mira do
jogador) e do resto, e o que os tiros pedem: os raios nos gatilhos, o `EHitWindow` (os vidros) e os efeitos dos
impactos na galeria. Os nomes são do nightfire-cxbx (NightfireResearch) e do `DRIVING.SYM`; a verdade é o PS2.

### Como o PS2 faz

- **Os passageiros do `'AIEl'`**: 17 dos 25 elementos de Paris levam um a três (o +0x80 do elemento, 0x20 bytes cada:
  a classe, o modelo, a arma, o fator de dano +0x0c, a vida +0x14, a precisão +0x1c, que dá o alcance): cinco
  atiradores de janela e nove de teto solar nos 14 `paris_henchbigbox` (o capanga mascarado
  `2.h_pmask.h_pmask.t_paris.95`, com a `ssmg` ou a `bazooka`) e três motoristas nos `paradis_car`
  (`3.c_domm.h_dommin.t_dommin.100`). As classes de `AICharacterEnemyGround.obj`:
  `AICharacterEnemyWindow` (a tabela virtual 0x00369200, 0x170 bytes), `AICharacterEnemySunroof` (0x00368f08, 0x180),
  `AICharacterEnemyDriver` (0x003681f0, 0x160); as da neve (0xe/0xf/0x10), o `AICharacterPassenger` e o
  `AICharacterBond` não aparecem em Paris.
- **A criação** (`AIElementController::AIElementController` 0x0012b8b0 carrega o personagem de cada passageiro no
  `ActCharacter`; o `WakeUp` 0x0012cb08, depois do carro): para cada passageiro vivo, o objeto da classe, o ator
  (`ActActorDatabase::GetNewActorHandle` com o modelo da tabela 0x00327ba8, a arma da 0x00327b90 e a função da matriz
  `GetRotPos` da classe; o assento é o vetor `AIC_EWIND_POS`/`AIC_ESUNR_POS`/`AIC_BOND_POS` dos atributos do carro), o
  `SetActor`, a primeira animação, o `Human` (o corpo simples que leva os tiros ao personagem, dono o carro) e a precisão
  na arma (o +0x15c da `ActWeapon` aponta para o passageiro). O `GoToSleep` apaga os `Human` com o
  `Simulation::DeleteHumanObject` (0x002019c0).
- **A cada passo** o `Human::Simulate` roda o `AICharacter::Execute`: a matriz pela pose do ator e a do carro
  (`UpdateRotPos`), as interrupções (a morte com o carro, a vida) e o estado pela tabela virtual (+0x44 + 8 x estado).
  O da janela fica escondido até o jogador entrar no alcance (`TargetIsInRange`: a distância ao quadrado até o +0x64 e
  o ângulo à direita do carro), se debruça, arma, mira (o ombro anda dois graus por passo; o pescoço e o ombro direito
  pelas sobreposições globais) e atira a cada 60 passos; o do teto abre o teto (o estímulo 8 do sistema 0x13 do carro),
  registra-se como alvo da mira do jogador (`PBondCar::EnableTargetBeacon` 0x0018ad50 / `DisableTargetBeacon`
  0x0018adc0), sobe, gira o ator até o alvo, atira, recarrega e se agarra quando o carro acelera
  (`VehicleIsAccelerating`); o motorista prende os pulsos ao volante (os efeitos `steerleft`/`steerright` do modelo do
  carro, `RSceneObj::LocateFX` 0x001cae00) e toca três animações. O tiro: a chave "Fire" do `ActPoser::DoEventPose`
  chega ao `ActActor::Fire`, a arma toca o estímulo 10 e a chave dela cria o `EFireWeapon` (seção 22), que mira no
  carro do jogador com a precisão do passageiro; os `RayShell` e os `Shell` acertam pelo `PBondCar::ApplyDamage` com o
  fator de dano do passageiro, `(dano + 100) / 100` (os passageiros com -100, os dos elementos 2 e 24, não ferem).
- **As sobreposições globais da pose** (`ActGlobalPoseOverrideArray`, 0x0010f140-0x0010f698, e os
  `ActActor::Create/Delete/SetGlobalPoseOverride(s)` 0x001090c0-0x00109120): até N ossos com a matriz e o peso; o
  `DoGlobalOverrides` (no `DoSkeletonPose`) mistura a matriz da pose com a pedida (`BlendMatrices`: os quatérnios pelo
  `fast_qslerp` e a translação), no espaço local ou global; a troca de linhas do `SetGlobalPoseOverride` é do original.
- **Os raios nos gatilhos** (`WTriggerManager::Process(int)` 0x0022def0, chamado pelo `Update` 0x0022f0c0 para cada raio
  ativo): o segmento do raio (`RayShell::fActiveRayShells`) pelas células da grade, os gatilhos com a bandeira 0x40
  (0xc0 para os do jogador; os 0x80 só pelo jogador) que o `CheckCollide(float, COORD4 *, WTrigger *)` 0x0022e3a0 toca
  (a esfera e o cilindro pela reta mais perto, `WWorldMath::NearestPointLine3D/2D` 0x00231cc8/0x002318b0; a caixa pelos
  planos; a direção pelo `WTrigger::TestDirection(COORD4 *)` 0x0022caa8), e o `FireEvents` com o raio
  (`RayShell::GetTracer`: o evento recebe o começo, o fim e o dano). Em Paris são os `EWakeupSmackable` (35) dos objetos
  do cais: um tiro acorda o poste.
- **O `EHitWindow`** (0x0014f480, o destrutor; o construtor e o `CheckHitWindow` eram da seção 24): no primeiro tiro numa
  janela, o som `SFX_GlassShatter1/2` (alternados pelo contador 0x0032bbc0) do banco Smackables, perto do chão (y < 10)
  o `SFX_StoreAlarm` do Generic a cada oito (0x0032bbc4), e na galeria o `Wd_Glass` no ponto e, no modo 1, o `Wd_Wsmk`
  no meio dos cantos; do segundo ao quinto tiro, o `SFX_Headlight` do Collisions. Os sons são `AOneShotSound`
  ("Explosions").
- **Os efeitos dos impactos**: o `Shell::ApplyWorldEffects` põe o efeito do material ("bullet", "bullethe",
  "bulletcar"...) na galeria pelo `GFX::TriggerIncidental` (0x00233108: o `GFX::Trigger` com a reserva da tela pela
  bandeira 1 do efeito). As partículas desses efeitos sorteiam o `RRandom` do render, o mesmo que decide a velocidade dos
  `Shell`, o dano visual e os `EFireRandomTrigger`: no PS2 os efeitos mudam o jogo, e no port também.

### Os arquivos

- `game/ai_passengers.{h,cpp}`: as três classes (os leiautes do PS2, os estados, as tabelas de animação copiadas do
  `.data`), o laço dos passageiros do `WakeUp`, o `passenger_api()` (o que eles chamam do ator, do carro e do resto: os
  padrões são o port, a conferência troca pelos ganchos) e o `passenger_report()`.
- `anim/actor.{h,cpp}`, `anim/act_actor.{h,cpp}`: as sobreposições globais; o `ActWeapon_LocateFX` (a arma e as cenas
  das armas) e o `ActWeaponData::accuracy`.
- `game/ai_character.{h,cpp}`: o `DeleteHumanObject`, o vetor dos `Human` (a Simulation +0xdf0) e o mapa das zonas.
- `game/bond_car.{h,cpp}`: o alvo da mira do carro e os vetores dos assentos dos atributos.
- `game/triggers.{h,cpp}`: os raios; `game/hit_window.cpp`: o `EHitWindow`; `fx/gfx_gallery.{h,cpp}`: o
  `TriggerIncidental`.
- `data/mips_interp.cpp`: o `PCPYH` (o `memset` do newlib que o `DoInitial` do motorista chama).

### A conferência (a parte 22 do `nf_driving_check`, `--so passageiros`)

Com `--casos 4000` (o original no interpretador MIPS com a VU0, o porte ao lado; os personagens em estados, carros,
atores e alvos sorteados, os ganchos do ator e do resto iguais dos dois lados, as chamadas comparadas em ordem):

| Caso | Casos | Diferentes |
|---|---|---|
| janela: o construtor, `Execute`, os estados pela tabela virtual, `TargetIsInRange`, `TargetIsAcquired`, `PlayZoneInjuryAnim`, `GetRotPos`, `SetActor`, o destrutor | 1.314 cada | 0 |
| teto solar: os mesmos (sem o `SetActor`) e o `VehicleIsAccelerating` | 1.355 cada | 0 |
| motorista: o construtor, `Execute`, os estados, `GetRotPos`, o destrutor | 1.331 cada | 0 |
| `AIElementController::WakeUp` (o laço dos passageiros: 4.858 personagens criados, os `Human`, os atores, as armas) | 4.000 | 0 |
| `PBondCar::EnableTargetBeacon` / `DisableTargetBeacon` | 4.000 | 0 |
| `NearestPointLine3D` e `2D`, `TestDirection(COORD4 *)`, o `CheckCollide` do segmento (236 gatilhos tocados) | 3 x 4.000 | 0 |
| as sobreposições: `Create`/`Delete`, `SetGlobalPoseOverride(s)`, `DoGlobalOverrides`, `BlendMatrices` | 4.000 + 7.967 + 4.000 + 4.000 | 0 |
| `EHitWindow::~EHitWindow` (2.948 sons, 3.010 efeitos da galeria) | 4.000 | 0 |

Os `Execute` pediram 22 tiros e 283 mortes. O `--casos 4000 --quadros 600` inteiro dá 0 diferentes em todas as partes.

Mais na parte 14 (`--so trafego`): o `Process(int)` e o `Update` com um a seis raios sorteados perto dos gatilhos de
Paris, sobre a grade de verdade (115 casos, 239 raios, 6 gatilhos tocados, 0 diferentes); e na parte 21 (`--so efeitos`) um terço dos disparos da
galeria pelo `TriggerIncidental`, com as telas comparadas a cada quadro (0 diferentes). A conferência achou: o
`kSunInjury` precisa ser a tabela inteira de 256 entradas da memória do PS2 (com uma zona fora da tabela o original lê
o vizinho), o `PCPYH` que faltava no interpretador e a ordem de inicialização do mapa dos `callbacks` do ator.

### No `nf_driving`

O `--drive` cria os passageiros com os carros acordados e os desenha pela camada dos atores (seção 24). A linha
`passageiros` do relatório conta as classes construídas, os tiros, as recargas, as miras, as mortes, as inclinações,
os gritos e os crossfades não portados e as guardas (por linha), e cada personagem (o estado, fora/dentro, o pente, a
vida, o lugar); a linha `vidros` conta os `EHitWindow`, os efeitos incidentais da galeria, os tiros dos outros no carro
do jogador e a vida que tiraram (`jogador baleado N dano D explosoes N dano D`) e os gatilhos tocados pelos raios.
Depuração: `--shot-look e@d,h,graus` desenha da câmera que olha o carro do elemento `e` (o jogo continua com a dele);
`NF_DRV_NO_IMPACT_FX=1` deixa os efeitos dos impactos e dos vidros só anotados, como antes.

Capturas (`out/agent_drv_passengers/shots/` do checkout principal, fora do git, 1280x720; `ROTA` é a da seção 24):

- `paris_passageiros.png`: `--fire-trigger 727@1 --autopilot "1@-321,20,40;-322,-100,40;-322,-200,25;-325,-300,20"
  --drive-input none:8 --shot-look 2@14,8,200`: o atirador do teto solar e o da janela debruçados, ao lado do Vanquish
  (16 tiros em 8 s).
- **"Mission Failed" pelo carro destruído pelos tiros** (`paris_perde_tiros.png`, o estado 4 na etapa 10, a vida 0):
  `--gunner-limit 0.9 --gunner 3,11,4,13,6,7,5,8 --gunner-tyres --autopilot "1@ROTA" --autopilot-fire
  --drive-input none:210`. O atirador da seção do helicóptero passa a seção, o piloto segue a rota atrás da van e para
  perto de (-898, -1000) ao lado do `paris_henchbigbox` do elemento 0, cujos dois atiradores do teto o acabam: 314
  balas tiram 1.690 da vida do carro e três foguetes da bazuca 712, o resto são batidas.
- **"Mission Accomplished"** (`paris_ganha.png`, o estado 3 na etapa 2, a vida 114 aos 260 s): o mesmo com
  `--gunner 3,11,4,13,6,7,5,8@0.15` e `--drive-input none:260`: os passageiros acertam 214 balas (1.160) e quatro
  foguetes (812), mas o Q-Pulse para a van na ponte antes.
- **"Mission Failed", a van foge** (`paris_perde_van.png`, o estado 4 na etapa 29, a vida 114): o mesmo sem
  `--autopilot-fire`.
- **"Mission Failed", na seção do helicóptero** (`paris_perde_heli.png`, o estado 4 na etapa 12): o comando da seção 24
  como está (o limite de 0,6 grau): o caminhão 6 sai inteiro.

**O que mudou na partida ganha da seção 24**: com o mesmo comando ela agora perde na seção do helicóptero (o estado 4 na
etapa 12): o fuzil das mãos acerta, de passagem, os gatilhos 502 e 510 do cais, dois `EWakeupSmackable` (a bandeira
0x40: um tiro acorda o objeto, como no PS2), os postes acordados mudam a física dali em diante e o caminhão 6 sai inteiro
(a câmera do AutoDrive não chega a ele antes do gatilho 720). Com `--gunner-limit 0.9` (o R1 com até 0,9 grau de erro:
ainda só os botões do controle) o atirador passa; na seção do carro os passageiros tiram até 2.000 da vida do Vanquish
(antes nada além das batidas), e o piloto, que só dirige e dispara o Q-Pulse, ganha ou perde conforme o caminho. A prova:
com o mesmo comando, antes dos raios (d62350b) ganhava; depois (b02b1a8) os raios tocam 502 e 510 (a linha `vidros`,
`raios_nos_gatilhos 502:2 510:1`) e perde, também com `NF_DRV_NO_IMPACT_FX=1` (não são as partículas dos impactos, que
também sorteiam o `RRandom`). A fala e a música da seção 27 não mudam nada aqui: na mescla, os três finais dão os mesmos
números de antes dela (as capturas não ligam o som, e sem ele os eventos de som só são aceitos).

### Divergências

- O crossfade (`ActActor::SetNewCrossFadeAnimation`, `CrossFadeAnimationController::StartNewCrossFade`): o `ActPoser`
  do port não tem o modo 2; a animação nova toca sem a mistura de 0,25 s (contado).
- O IK do motorista (`ActActor::CreateIKs`, `SetIKInfoArray`; o `ActIKSolverArray::Solve` não foi portado) só guarda a
  informação; as sobreposições dos pulsos são feitas.
- O `ACharacter::Say` ("HDeath", "HGrunt": o som do personagem) não foi portado (contado).
- O `EHitWindow` roda no fim do passe dos eventos, depois dos numerados (no PS2, no mesmo anel; só sons e efeitos).
- Em Paris, 14 a 16 vezes por partida, um personagem pede o carro (o +0x150) depois que o carro saiu da simulação (o
  PS2 leria o objeto apagado): guardado e contado; o estímulo do teto solar desse carro não sai.

### O custo

Não medido (o computador estava em uso: nenhuma janela).

### O que falta e a estimativa

Dos tiros: os riscos das balas (`RBulletStreak`: `Init` 0x001d7620, `Add` 0x001d7d78/0x001d8048, `Draw` 0x001d7c18 e
o `UVolatileMaterial::Draw` 0x002cb938, chamados pelo `RayShell::DrawTracers` 0x001993e8 e pelo `DrawBulletStreaks`
0x001c5aa8; os traçadores já ficam guardados; o `Init` lê os tipos do DTuningDB, `BulletStreakType` com a cor, a
textura, a largura, o comprimento, a velocidade e a contagem, e a velocidade dos `Shell` com rastro também vem dele: hoje
fica a sorteada, contada) e os clarões (`RayShell::DrawFlashes` 0x00199678), 0,5-1 dia; o crossfade do `ActPoser` e o
IK (0,5-1 dia); o som dos personagens. A linha "os passageiros que atiram" da seção 24 (1,5-2,5 dias) e o caminho dos raios (0,5
dia) saem: sobre as contas da seção 24 (Paris em 7-12 dias, as fases 2-4 em 7-13), Paris fica em **5-9 dias** e as
fases 2-4 em **5-10 dias**.

## 27. A fase 3: as falas e a música de Paris (os fluxos, o mixador do EE, o `AStream`, o `AFader`)

Paris tem som de missão: a música interativa do `.MUS`, as falas e os ambientes da cidade do `.SPE`, com as trocas de
mixagem que as regras pedem. Tudo do MIPS do `DRIVING.ELF`, conferido contra o original no interpretador (a parte 23 do
`nf_driving_check`); o lado do IOP é o da seção 17 (o mixador do EE entrega à tagbuf, e o IOP já portado leva à SPU2).

### Como o PS2 faz

O `ASoundManager::Init` cria dois `AStream` (um `ABaseSound` de 0xc0 bytes com o `AStreamPriv` de 0x14c): "Music"
(o BIG `driving/MIS01.mus`: `007logo`, `all`, `bondmove`, `silence`, `snipeintro`, `snipeloop`) e "Speech" (o
`driving/MIS01EN.spe`: as falas `par_r_*` e os ambientes `city`, `crowds`, `residential`, `for_heli`), cada um com
0x20000 bytes de buffer, e o `AFader` "SpeechVsAmbience", que divide o fluxo das falas entre a fala (a primária) e o
ambiente (a secundária, em laço). O `EAudioUpdate` de cada quadro pede ao `WWorld::GetEnviroDesc` o ambiente sob o carro
(a tabela `'Envi'` do grupo `'Map '`, pela instância de colisão do chão: o modo do `AFX` e o nome do ambiente), passa
o nome ao `AFader` e, depois do `BuildPaths` (que toca os `AStream` como sons a distância "infinita"), roda o
`AFader::Update` (as transições: a primária baixa o ambiente e a música pelas mixagens "Fade Effects" e "Fade Music",
espera a fala acabar e volta).

Cada `AStream::Play` manda ao SND o volume, o pitch e o passa-baixa do caminho e, sem pedido em curso, tira a frente
da fila de eventos (`AStream::Event`: o nome, a transição, laço, efeito, "segurado") e a pede ao `SNDSTRM_queuefile`
(um evento em laço é pedido de novo quando só ele resta na fila do SND; um segurado, o prebuffer dos NIS, espera o
`EReleaseStream` ou 10 s). O SND dos fluxos: o `SNDSTRM` (um canal de 0x138 bytes, 4 pedidos de 0x28, o tocador de
pacotes de 0x5c + 15 x 0x20 e o `STREAM` do librealfilez no fim da memória) lê os pedaços do `.asf` pelo cliente do
servidor (`SNDSTRMI_service`, que o `SNDSYS_service` de cada quadro roda, não o relógio de 100 Hz): o `SCHl` (o
cabeçalho PT: `SNDI_patchtohdr`; mono, 48 kHz, EA-XA, o modo de render 4) e os `SCDl` (as amostras de cada canal em
blocos de EA-XA de 28 amostras, depois de duas amostras de estado), entregues ao tocador (`SNDPKTPLAY`), que toca uma
voz do mixador do EE. O mixador (`MIX_audio`, por fatias de 512 amostras no tick do driver) puxa a cadeia de filtros de
cada voz (o passa-baixa RC 0x28, o reamostrador 0xa0, o desempacotador de EA-XA 0xf0), soma nas saídas em float com o
ganho (`mixc`, a VU0) e converte para 16 bits na tagbuf (`SFILTER_ft16`), que o IOP mistura na entrada do núcleo 0 da
SPU2.

Os eventos: 20 `EStreamEvent` ("music": `AStream::Event`; "speech": a primária do `AFader`; "nis": a primária com
efeito, nenhum laço; os que falharam antes ficam em `fgFailedStreams`), 273 `EReleaseStream` (solta o segurado), 241
`ESetAudioMix` (uma mixagem a (1 + p/100) x o volume do `.ini` em n quadros), 266 `EResetAudioMix` (todas as do `.ini`
de volta), 164 `EPlaySoundPos` (um `AOneShotSound` parado num ponto do mundo). O `EPlayerWin` e o `EPlayerLose` chamam o
`ASoundManager::SetMissionOver`: a música some em 3 s (`SNDSTRM_autovol`) e não volta.

### Os arquivos

| Arquivo | O que tem |
|---|---|
| `sound/snd_mix.h` | o mixador do EE (35 funções: `MIX_*`, `MIXI_*`, `SNDMIX*`, `mixc`, os filtros `SFILTER_unpackxapf/rsf/lpfRC/ft16` e a lista de filtros), as vozes de 0x60 e os filtros com o leiaute do PS2 no heap do SND |
| `sound/snd_stream.h` | os fluxos do SND (52: `SNDSTRM_*`, `SNDSTRMI_*`, `SNDPKTPLAY*`, `SNDLINKI_*`, `SNDI_patchtohdr`, `SNDI_defaultattributes`) |
| `sound/stream.h` | só do port: o `STREAM_*` e o `FILESYS_*` dos fluxos (o `StreamModel`: o `.asf` inteiro lido do disco na hora, os pedaços "entrando no buffer" enquanto cabem na capacidade do original) |
| `sound/audio_stream.h` | o `AStream`, o `AStreamPriv`, o `AFader`, a parte dos fluxos do `ASoundManager` e do `EAudioUpdate`, o `WWorld::GetEnviroDesc`, o `SetMissionOver` e os eventos 20, 273, 241, 266, 164 (29 funções) |
| `sound/audio.cpp` | o `AMix::Reset` e o `AMix::GetPresetVolume`; o `audio_init` cria os fluxos (`AudioInit::stream_file`, `big_open`) |
| `sound/snd_ps2.cpp` | o `SNDPLATFORM_packetplay`; o tick do driver esvazia a fila dos callbacks do tocador |
| `data/mips_interp.cpp` | MTSAB, MTSAH, QFSRV, PINTEH, PSLLW e PSRLW (o reamostrador do original) |

### O que toca em Paris

A partida ganha da seção 24 (`--gunner ... --autopilot-fire --drive-input none:210`) com `--audio-dump` e
`NF_DRV_STREAM_LOG` (o relógio do jogo):

| s | O quê |
|---|---|
| 0,03 | a música `snipeintro` (a entrada da seção do helicóptero); o `snipeloop` vai à fila aos 0,70 s, em laço |
| 0,06 | o ambiente `for_heli` (laço) |
| 2,18 | a fala `par_r_bond_4` (o evento aos 1,68 s: o `AFader` baixa o ambiente antes); a música cai a 0,4 |
| 3,80 | o ambiente volta |
| 34,55 | o NIS `cornerstraight`, segurado; solto pelo 273 aos 39,73 s |
| 39,45 | o `snipeloop` (acabou a entrada), em laço até o carro |
| 50,28 | `par_r_bond_5`; aos 54,21 o NIS `construction` (solto aos 55,91), aos 77,88 o `chainshotpass` |
| 84,00 | a música `all` (a seção do carro, em laço) e a fala `par_r_comp_19` |
| 86,66 em diante | os ambientes pelo chão: `city`, `residential` e `crowds` (21 pedidos até o fim, as voltas depois das falas incluídas) |
| 114-202 | mais 9 falas: `par_r_dom_19`, `par_r_dom_2c_alt2`, `par_r_dom_6ab` e `par_r_comp_98` (seguidas), `par_r_m_14`, `par_r_q_27`, `par_r_comp_16`, `par_r_q_28`, `par_r_comp_14` |
| 206 | "Mission Accomplished": a música some em 3 s |

O WAV (`python tools/sound.py analyse <wav> --window 1`, contra a mesma partida no executável do main, só o carro): no
começo -20,0 dB RMS (o carro sozinho -30,2), a seção do helicóptero -19,6 (-37,4), a fala `par_r_bond_5` -19,8 (o carro
parado, mudo), a seção do carro -16,4 a -12,8 (-28,4 a -19,2), a música some depois da vitória (-25,9, -37,3 e -55,3 dB
nos segundos 207-209). Picos de -4 a 0 dBFS; 70 amostras cortadas em 210 s, todas na seção do carro com a música, uma
fala, as explosões e o carro juntos (a cadeia tem ganho 1, seção 17). A missão anda igual à do main (as mesmas regras,
disparos e gatilhos; 47 eventos de som a mais executados).

### A conferência (a parte 23 do `nf_driving_check`, `--so fluxos`)

O código original roda no interpretador (com a VU0 em modo macro) lado a lado com o porte, com o mesmo `StreamModel`
dos dois lados (o lado emulado copia os arquivos para a RAM emulada); os `.asf` grandes de Paris entram com os
primeiros ~400 KB. Com `--casos 4000`:

- o `SNDI_patchtohdr` sobre o `SCHl` dos 236 `.asf` de todas as missões: 0 diferentes;
- o SND dos fluxos inteiro: o SND iniciado como o `ASystem`, um fluxo como o do `AStreamPriv` e 16.000 operações
  sorteadas (741 pedidos, 140 segurados; 311 purges; 159 pitch fora de 1, os dois caminhos do reamostrador; 154
  passa-baixas; o volume, a rampa, o azimute, o status; 3.144 `SNDSYS_service` e 7.167 ticks do driver, 5.292 com voz
  tocando, pico 14.838; 114.500 KB lidos), depois de cada uma o canal, os atributos, os pedidos, o tocador e os
  pacotes vivos, a fila dos callbacks, as vozes do mixador e a cadeia de filtros (o desempacotador com o seu
  `CEAXABLKDecf`, o reamostrador, o passa-baixa), as somas em float, as amostras de 16 bits que vão ao IOP e as vozes do
  SND: 0 diferentes;
- a camada do jogo sobre o SND inteiro: o `ASoundManager::Init` dos fluxos de Paris (o `SetPath`, os dois `AStream`,
  o `AFader`) e 8.000 operações (3.214 quadros com o `AStream::Play` dos dois fluxos, o `AFader::Update`, o
  `SNDSYS_service` e os ticks; 795 `EStreamEvent`, 336 da música e 313 falas; 338 trocas do ambiente; os eventos 273,
  241 e 266; o `Next`, `Last`, `Stop`, `FadeOut`, `GetLatency`, o `Call911`; os globais da missão acabada, do fade do
  HUD e da pausa): os dois `AStream` (a fila lida na deque do GCC 2.95 do original), o `AFader`, os globais, as 65
  mixagens e tudo o que a parte do SND confere, nos dois fluxos: 0 diferentes. As mixagens do lado emulado são cópias
  das do porte (o `.ini` não é lido lá; o `AMix::SetTransition` e o `GetPresetVolume` originais rodam sobre elas).

O que ela achou no porte: as seções críticas que faltavam (o 0x00307be0 é o `SNDSYS_leavecritical`; sem elas o
servidor de 100 Hz parava, também para o som do carro do `nf_driving`), um pedaço liberado do `STREAM` que o canal de
baixo ainda lê (os pedaços ficam legíveis até entrar outra capacidade), e dois casos do original: as opções que o
`AStreamPriv` dá ao `SNDSTRM_create` levam nos bytes +6/+7 as letras do texto "<arquivo> buffer" da pilha (o porte as
reproduz; nada as lê), e quando o `SNDSTRM_queuefile` recusa um pedido (-13: os quatro ocupados, 33 vezes na conferência)
o `DecodeError` esvazia a fila e o `AStream::Play` tira a frente da deque vazia (o começo passa do fim: o original
voltaria a ler entradas mortas). O porte guarda e conta; a conferência repara a deque do original do mesmo jeito.

Não conferidos contra o original: o `WWorld::GetEnviroDesc` (quatro linhas), o `SetMissionOver` e o 164 (um banco do
mundo: lidos). Os testes (`tests/test_driving_streams.cpp`): um `.asf` feito à mão (uma onda quadrada de EA-XA) pela
cadeia inteira até a SPU2 sai na frequência certa, o pitch a dobra (o reamostrador direto) e a divide (interpolando), o
fluxo acaba no `SCEl`; um pedido segurado só toca depois do `modifyhold`; o `AStream` toca um evento em laço além do fim
do arquivo e o `SetMissionOver` o cala.

### Divergências

- O `STREAM` lê o arquivo inteiro na hora (um DVD instantâneo): os pedaços entram no buffer na ordem enquanto cabem na
  capacidade do original; um arquivo que não existe é recusado no pedido (o original falha alguns quadros depois).
- A fila do `AStream` é um `std::deque` (o porte não guarda o `AStream` no leiaute do PS2, como o resto da camada); o
  pop da fila vazia depois do `DecodeError` e um `AStream::Get` de um nome que não existe são guardados e contados.
- Os filtros do mixador que nenhum fluxo usa (os desempacotadores de 16 bits, de banco e de MicroTalk, o passa-alta, a
  distorção, a esticada, o reverb do EE) contam e não fazem nada; o reamostrador com pitch diferente de 1 é o do
  original, com o erro da fase que não anda entre as chamadas (`sound/snd_mix.h`).
- Os pedaços que os pacotes do mixador e do STREAM leem ficam no arquivo copiado, não num anel de memória.

### O custo

Não medido (o computador estava em uso: nenhuma janela). A partida de 210 s com o WAV roda em 12,6 s fora da tela.

### O que falta e a estimativa

Do som de Paris: o mixador do IOP (os 249 timbres de EA-XA dos bancos `h*` dos personagens, os ambientes `a*`, o fora
de estrada) e os outros objetos de som (o tráfego, as armas do carro, a buzina e o alarme, o `WSoundGroup` e o
`ASmackable` do mundo, os personagens, os sons da galeria `gs`); o som dos filmes vem com eles (`ESetVideo`). A linha
"os fluxos de fala e música e os mixes" da seção 19 (1 dia) está feita: sobre as contas da seção 24 (Paris em 7-12
dias, as fases 2-4 em 7-13), Paris fica em **6-11 dias** e as fases 2-4 em **6-12 dias** (antes de juntar a seção 26).

As seções 26 e 27 foram feitas ao mesmo tempo e cada uma contou a sua estimativa sem a outra (a 26: Paris 5-9 dias, fases 2-4 5-10; a 27: Paris 6-11, fases 2-4 6-12). Somadas, as duas tiram os passageiros, os raios dos gatilhos e as falas e a música: Paris 4-8 dias, fases 2-4 4-9.

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
| `nf_driving` | o desenho (seção 10): um carro girando numa janela (`--data`, `--viv`, `--model`, `--adapter`); `--screenshot` (fora da tela, com o relatório `.txt`: métodos, peças, instâncias, texturas, o estado do GS de cada passada); `--verify` (cada lote da família no interpretador da VU1 contra a referência); `--check` (todos os carros de todos os arquivos, os personagens (seção 16) e as 8 pistas com o mundo de colisão; `--check-viv`); `--ground` (o carro assentado num chão plano, com as rodas, as lâmpadas e a sombra, seção 16); `--track <pista>` (a pista desenhada, com `--car`, `--car-pos`, `--cam`, `--verify`); `--drive` (o carro dirigível, seção 13: `--drive-input`, `--seconds`; com o tráfego, os gatilhos e os objetos derrubáveis da seção 18: `--no-traffic`, `--fire-trigger N`, `NF_DRV_TRIGGERS=1`, `NF_DRV_SMACK_LOG=1`; o som, seção 17: `--audio-dump <wav>`, `--no-audio`, `NF_DRV_SND_LOG`); `--census` (os 115 render methods por microprograma e família); `--view`, `--part`, `--no-spec`, `--no-refl`, `--no-fallback`, `--bench-frames`; `--no-cull` (sem a árvore e as cortinas, seção 15); `--actor <cadeia do jogo>` com `--anim <banco>:<animação>`, `--seconds`, `--list-anims` (um personagem posado e animado, seção 20); na missão (seções 21 e 24): `--autopilot e@x,z[,v];...`, `--autopilot-fire`, `--gunner e1[,e2...][@altura]`, `--gunner-tyres`, `--gunner-limit graus` (seção 26), `--shot-look e@d,h,graus` (seção 26), os direcionais `LU/LD/RL/RR/RU/RD` no `--drive-input`, `NF_DRV_GUN_LOG`, `NF_DRV_GUN_LIMIT`, `NF_DRV_PILOT_LOG`, `NF_DRV_AI_LOG`; o `--audio-dump` leva também a música, as falas e os ambientes da missão (seção 27; a linha `fluxos` do relatório, `NF_DRV_STREAM_LOG`) |
| `tools/ghidra_scripts/ExportDecompiledAt.java` | decompila funções pelo endereço (cria a função se o Ghidra não a tem), sem gravar no projeto |
| `nf_driving_check` | a física conferida contra o `DRIVING.ELF` (seções 11 e 13): a cobertura do interpretador das VUs (os três programas da VU0 e os 115 render methods), o microcódigo original × o porte à mão, o passo inteiro dos dois jeitos, as funções originais do EE no interpretador MIPS com a VU0 × o porte; a parte 5 (o chão das 8 pistas), a 6 (o carro sobre o chão de Paris), a 7 (os choques, seção 14), a 8 (a câmera; `--so camera` só ela), a 9 (o escalonador; `--so agenda`), a 10 (a entrada; `--so entrada`), a 11 (a visibilidade do mundo e as entradas por quadro dos métodos do mundo, seção 15; `--so visibilidade`), a 12 (o carro desenhado, a sombra, as luzes, a vibração e o Master.def, seção 16; `--so carro`), a 13 (o som: o EA-XA, o núcleo do SND e o som do jogo, seção 17; `--so som`) a 14 (o mundo vivo: a rede de estradas, o navegador, o tráfego, os gatilhos e os objetos derrubáveis, seção 18; `--so trafego`), a 15 (a missão, seção 19; `--so missao`), a 16 (os personagens: a animação, o esqueleto e o `ActPoser`, seção 20; `--so personagens`), a 19 (a seção do helicóptero: o `RPathEngine`, o trilho, os eventos do trilho e de câmera, a câmera do AutoDrive e as animadas, seção 23; `--so helicoptero`), a 20 (as armas ligadas à IA: os tiros, os atores e as mãos, a mira, o `WTargetPicker`, a direção automática e as armas dos motoristas, seção 24; `--so integracao`, `--so integracao.tiros|.atores|.ia`), a 21 (os efeitos: as partículas, a galeria, as luzes, as marcas, o rastro dos mísseis e os eventos de efeito, seção 25; `--so efeitos`) e a 23 (os fluxos: o `SNDI_patchtohdr`, o SND dos fluxos e o mixador do EE, o `AStream`, o `AFader` e os eventos de som, seção 27; `--so fluxos`, `NF_P23_DEBUG`); `--consultas N` |
| `tools/pcsx2_pine.py` | grava os corpos rígidos de uma missão de carro a cada quadro pela IPC do PCSX2 (`status`, `record`, `test`) |
