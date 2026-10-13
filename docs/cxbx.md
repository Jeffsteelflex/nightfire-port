# O nightfire-cxbx (NightfireResearch): o que aproveitar

Estudo de outro projeto do mesmo jogo, feito em 12/10/2026: o **nightfire-cxbx**, da NightfireResearch
(https://github.com/NightfireResearch/nightfire-cxbx, ramo `dev`, commit `7989839`), lido num clone só para leitura em
`out/ref/nightfire-cxbx` (fora do git, sem o `third_party`). Os autores disseram ao usuário que podemos nos basear no
trabalho deles; tudo o que vem deles leva o crédito **"nightfire-cxbx (NightfireResearch)"**, aqui, nos comentários dos
nomes no Ghidra e no `README.md`.

O que eles fazem: portam a versão de **Xbox**. Como o Xbox é x86, um carregador próprio (`action.exe`, `driving.exe`)
mapeia o `default.xbe` (a pé) e o `Driving.xbe` (a engine de carro da EA) num processo Windows de 32 bits, responde às
chamadas do kernel do Xbox, troca as bibliotecas (D3D8 por D3D9, traduzindo em tempo de execução os programas de vértice
do NV2A e os "register combiners" para HLSL; DirectSound por XAudio2; XInput) e reescreve as funções em C++ aos poucos
(~10% do jogo a pé; na engine de carro, a física, a colisão, o mundo, as câmeras, a animação, a entrada e a biblioteca de
som SND inteiros). Segundo o README deles, o jogo inteiro roda, missões de carro incluídas.

**O nosso porte continua sendo do PS2** (o disco que os jogadores têm): o PS2 é a verdade, o Xbox é referência de leitura,
como na seção 9 do `docs/driving.md`. Regras deste estudo: nada do clone foi compilado, executado ou copiado para o
repositório; o `data/` deles só tem um `credits.csv` (texto do jogo: não usado); nomes de funções, tipos, campos,
deslocamentos e fatos sobre o código são fatos e entram aqui com a fonte. Nenhum código deles foi para `src/`.

## Resumo: as dez coisas mais úteis, em ordem

1. **Os nomes do `DRIVING.ELF`** (seção 1): 2.376 funções novas com nome no nosso `out/ghidra_driving` (o código do
   jogo, antes do `crt0`, de 63% para 84% dos inícios de função com nome; as bibliotecas de 43% para 75%), 71 nomes
   nossos corrigidos, 480 variáveis globais com nome; e 3.629 pares Xbox ↔ PS2 em vez de 2.332 (o código do jogo com
   par de confiança >= 2: de 57% para 74% dos bytes). `tools/cxbx_names.py` e `tools/xbox_match.py match --cxbx`.
2. **Lacunas nossas na engine a pé que mudam o jogo**, achadas comparando e conferidas no PS2 (seção 3.1): funções
   curtas que ficaram como stub e que o Xbox deles roda. A pior é **`ReadTuningVars`** (a mira automática, a velocidade
   da mira, o dano por dificuldade e por região e a precisão dos inimigos ficam com os valores do `.data` em todas as
   fases); depois `NDrone2_NavNodeCache` (sem rota A* até o jogador), `Rand_FRand_MVar2_Vec` (o tremor da câmera não
   move e o `Rand` sai de passo), `psiDecompressWoman` (o fundo animado da pausa), `DroneFunc_HandleExplosives`,
   `Drone_MessageObjVicinity`, `Occlude_Create`, o controle da Zoe, `Drone_FellOutMap`, `View_DrawGlist`,
   `Input_Ready`; e um erro latente no `Mission_MonitorObjectives`.
3. **A física e o mundo do carro que nos faltam, em C++ legível, e as estruturas** (seções 2.1 e 2.2):
   `RigidBody::CollideWithObject`, `CollideWithWorld`, `ResolveWorldOBBCollision`, `CalculateAndApplyWorldDamage`,
   `WCollisionMgr`, `WGrid`, `WWorldPos::FindClosestFace`, `SurfaceBumpHeight`, `PBondCar::ProcessPhysics`/
   `AddWheelForces`, de onde vêm as medidas e os pontos de colisão do Vanquish (o que travava o carro na seção 11 do
   `docs/driving.md`); `tools/structs_driving.json` (280 tipos, 3.202 campos: `PBondCar` 0x420 com 94 campos,
   `SMissionManager`, `Simulation`, `RVehicle`, `RLightManager`...), com duas regras para levar um leiaute ao PS2.
4. **Os formatos do CARP que não decodificamos** (seção 2.3): os 20 resolvedores por etiqueta, o `BaseDesc`, as
   palavras das instâncias que não líamos, o `CDat` (a grade e a malha de colisão), a rede de estradas `RNgp`, os
   gatilhos, as cortinas.
5. **As entradas do render por quadro que o nosso protótipo ainda supõe** (seção 2.4): a `SpecularMat` (não é a rotação
   da câmera: conferido no PS2), a luz do chão que falta, o brilho por carro, o `ReflectionInfo2`, o mapa de reflexo
   dinâmico, a ordem do quadro, as glares, a sombra, o céu, a água e a neblina.
6. **O laço e o `Scheduler`** (seção 2.5): as quatro agendas, as tarefas com prioridade, o tratamento do tempo
   (atrasos de 12 ticks ou mais são jogados fora), um bug do original no `timeScale`, a fila de eventos, a ordem do
   `main` e do `GameLoop_StartUp`.
7. **A página de troca** (seção 2.6): o leiaute deles é o do nosso bloco UberBond (0xa4c bytes), com campos do lado do
   `DRIVING` que não tínhamos.
8. **O som da EA** (seção 2.7): a biblioteca SND reescrita (305 funções), cujas camadas de cima são o mesmo fonte do PS2,
   o decodificador EA-XA e a música; e os filmes da engine de carro.
9. **O resto da engine a pé** (seção 3): os menus do multijogador e o seletor de filmes documentados, os bots e a
   navegação deles, `BodyGlow_*` e `Car_*`, a `PTPDATA` tipada, nomes para muitos campos `unknown_xx` nossos; e uma lista
   de erros de leitura deles, conferidos no PS2, para levar na conversa.
10. **Os métodos** (seção 4): a validação do alinhamento deles mostrou um ponto fraco do nosso `driving_sym.py` (as
    corridas de funções com o mesmo tamanho), e a lista dos travamentos que eles corrigiram diz algo sobre o original.

Estimativa das missões de carro (seção 5): de **26-39 para 20-32 dias**.

### A ordem sugerida para aproveitar

1. **A engine a pé, já**: portar as funções curtas da seção 3.1, cada uma do MIPS do PS2, com teste (`ReadTuningVars`
   primeiro, que muda os números de toda fase; depois `NDrone2_NavNodeCache`, `Rand_FRand_MVar2_Vec`, o controle da Zoe,
   `DroneFunc_HandleExplosives`, `Drone_MessageObjVicinity`, `Occlude_Create`, `Drone_FellOutMap`, `View_DrawGlist`,
   `Input_Ready`, `psiDecompressWoman`), refazendo as medições do `CLAUDE.md` que dependem delas.
2. **A física do carro**, no passo que a seção 11 do `docs/driving.md` já previa (o chão de verdade, o `PBondCar`,
   `CollideWithObject`): ler cada função no C++ deles (seção 2.2) e o par no `out/xbox/match.tsv`, portar do MIPS,
   conferir no `nf_driving_check`; os formatos do CARP da seção 2.3 entram aqui.
3. **As entradas do render por quadro** (seção 2.4) e os métodos do carro que faltam, conferindo cada suposição no
   código do PS2 que eles apontam (`RLightManager`, `RCARPFile::RegisterSymbols`, `RReflection`).
4. **O laço e a troca** (seções 2.5, 2.6) quando o carro andar: o `Scheduler`, o `GameLoop`, a página.
5. **O som da EA e os filmes** (seção 2.7), na fase 2.
6. **O multijogador e os bots** (seção 3.3), se o usuário decidir fazê-los: os docs deles são o mapa.

## 1. Os nomes do DRIVING.ELF e do Driving.xbe

### O que eles têm

| Fonte (no clone) | O que é |
|---|---|
| `tools/functions_driving.json` | o Ghidra deles do `Driving.xbe` sincronizado (`ghidra/NightfireSync.py`): 9.735 funções, 6.714 com nome, com protótipo e convenção de chamada |
| `driving-symbol-matching/results/batches/batch-P*.json` e `.log.json` | 23 lotes de nomes aplicados ao `DRIVING.ELF` deles: 3.970 endereços (os nomes da planilha de símbolos do PS2, que vem do `DRIVING.SYM`, postos pelo alinhamento deles ou lendo o código; a evidência de cada um no lote) |
| `driving-symbol-matching/results/batches/batch-0*.json` | os lotes do `Driving.xbe` (4.806 itens), com a evidência |
| `driving-symbol-matching/results/globals/ps2-map.json` | 480 variáveis globais do SYM postas no ELF (389 "exact", 81 "predicted", 10 "snapped") |
| `driving-symbol-matching/results/globals/vt1-xbox.json` | 348 rótulos de tabela virtual do Xbox (`ABaseSound::vftable`...) |
| `tools/structs_driving.json`, `tools/vtables_driving.json`, `tools/alloc_sizes_driving.json` | as estruturas, as tabelas virtuais com as classes derivadas, o tamanho de cada alocação |
| `driving-symbol-matching/README.md` | o método: a planilha, o "infill" pelos tamanhos, o alinhamento por janela com margem, a validação com linhas escondidas |

**As mesmas imagens que as nossas.** O `Driving.xbe` deles é o europeu e o nosso o americano, mas o leiaute é o mesmo:
6.924 dos 7.050 inícios de função que o nosso Ghidra achava estão na lista deles, e os nomes batem onde os dois têm. O
`DRIVING.ELF` deles é o do PS2 europeu (`/PS2_EU_51258/DRIVING.ELF`, SLES-51258), e também tem o leiaute do nosso
(SLUS-20579): 3.967 dos 3.970 endereços dos lotes caem em inícios de função do nosso ELF (3.276 que o Ghidra já tinha,
691 logo depois de um `jr ra`, funções que ele não tinha achado), e 1.997 nomes batem com os do `driving_sym.py` no mesmo
endereço, espalhados pelo binário inteiro. Os endereços do `ACTION.ELF` deles, porém, são do europeu, que tem a sua
`.symtab`: nos docs da engine a pé, casar pelo **nome**.

### A comparação (`python tools/cxbx_names.py compare`)

O DRIVING.ELF, os lotes deles contra o `driving_sym.py` (seção 2 do `docs/driving.md`):

| No mesmo endereço | Funções |
|---|---:|
| nome nosso de confiança 2-3, o mesmo | 1.997 |
| nome nosso de confiança 2-3, outro (disputa) | 177 |
| sugestão nossa de confiança 1, a mesma | 482 |
| sugestão nossa de confiança 1, outra | 526 |
| nada nosso | 788 |

As 177 disputas: 113 são alinhamento contra alinhamento (o nosso pelo tamanho, o deles pelo alinhamento com margem); em
56 eles leram o código ("body review", "window review", "corrected"), em 8 o nosso par do Xbox tem o nome deles. Elas
se concentram nas bibliotecas da EA (0x250000-0x29ffff: na faixa 0x280000-0x29ffff, 42 de 42 diferem), onde a ordem
do link mudou entre o SYM e o ELF de varejo, e em corridas de funções minúsculas de tamanho igual. Exemplo conferido no
MIPS: 0x119790-0x119850 é uma corrida de 25 funções de 8 bytes (`jr ra; nop`) da `AICharacter`; o nosso "tamanho exato"
pôs `SetAvoidZoneDistance(float)` e `SetUseAvoidZone(bool)` em duas delas (nenhuma grava nada) e `DoDead` em 0x119850, que
devolve 0 (`daddu v0, zero, zero`): é a `IsTooFarAway` deles, e a corrida inteira estava deslocada de duas posições. O
próprio README deles mede o problema: a "exact-run" acertava 91% dentro das corridas de tamanhos iguais, e foi trocada
pelo alinhamento com margem (99,5% com margem >= 3).

Os nossos pares (`out/xbox/match.tsv`) contra o nome que eles deram à função do Xbox: confiança 3, 173 iguais e 17
diferentes; confiança 2, 769 e 50; confiança 1, 210 e 36. Parte das diferenças é convenção: no MSVC a entrada do
destrutor na tabela virtual é o "scalar deleting destructor", que chama o destrutor de verdade; o nosso casador pareia o
destrutor do GCC (que também apaga) com o primeiro, eles dão o nome `~Classe` ao segundo. Outras são erros nossos
(as cadeias dos nomes das pistas parearam o `main` do PS2, que escolhe a pista dentro dele, com a função do Xbox que
eles chamam `MissionNumToString`, 0x596a0; o `main` do Xbox é 0x5a1b0) ou deles.

### A importação (`tools/cxbx_names.py`)

```
python tools/cxbx_names.py compare                    # as contagens acima e as 177 disputas (out/driving/cxbx_compare.txt)
python tools/cxbx_names.py pairs                      # out/xbox/cxbx_pairs.tsv: pares pelo nome igual e único dos dois lados
python tools/xbox_match.py match --cxbx               # o casador com esses pares como âncoras de confiança 2
python tools/cxbx_names.py names                      # out/driving/cxbx_names.tsv e out/xbox/cxbx_xbox_names.tsv
NF_DATA=... tools/ghidra_driving.sh names out/driving/cxbx_names.tsv
NF_DATA=... tools/ghidra_xbox.sh names out/xbox/cxbx_xbox_names.tsv
python tools/cxbx_names.py test
```

As regras (o cabeçalho do `cxbx_names.py` tem os detalhes): um nome nosso de confiança 2-3 que concorda fica; numa
disputa, o nome deles entra só se o nosso veio do tamanho e o deles da leitura do código ou do par do Xbox, senão o deles
vai para o comentário (o tipo `A` novo do `ApplySymbolTable.java`, que acrescenta uma linha ao comentário de cabeçalho);
sem nome nosso de confiança 2-3, entra o nome do lote deles, ou o que eles deram ao par do Xbox (par de confiança >= 2, ou
de confiança 1 quando bate com a nossa sugestão); os nomes que só existem no Xbox (os "scalar deleting destructors", as
funções de desenrolar exceções do MSVC, o DirectSound `dsnd*`, D3D, XAPI) nunca vão para o PS2. Os pares pelo nome não
usam endereços em disputa. Todo nome importado leva no comentário de cabeçalho a fonte, o lote e a evidência deles.

Resultado, aplicado em 12/10/2026 (`out/` não vai para o git; refazer com os comandos acima):

| | Antes | Depois |
|---|---|---|
| `out/ghidra_driving`: inícios de função com nome antes do `crt0` (o código do jogo) | 3.496 de 5.578 (63%) | 4.673 (84%) |
| idem, depois do `crt0` (SDK, GCC, EAGL, REAL, SND) | 1.614 de 3.754 (43%) | 2.813 (75%) |
| nomes nossos trocados pelos deles (disputas com leitura do código ou par do Xbox) | | 71 |
| comentários acrescentados (a disputa sem decisão, o par fraco) | | 181 |
| variáveis globais com nome | 0 (só as 282 tabelas virtuais) | 480 |
| funções criadas no Ghidra (os endereços deles que a análise não achou) | | 635 |
| pares Xbox ↔ PS2 (`xbox_match.py`) | 2.332 | 3.629 (1.143 pelo nome, 1.582 pelos vizinhos em vez de 1.408, 63 pelas tabelas virtuais em vez de 34) |
| código do jogo com par de confiança >= 2 (funções / bytes) | 994 (26%) / 57% | 1.866 (49%) / 74% |
| física e veículos / IA / eventos `E*` / render / `libeaglanima` (bytes com par >= 2) | 88% / 75% / 41% / 57% / 1% | 92% / 89% / 69% / 78% / 66% |
| `out/ghidra_xbox`: funções com o nome deles | os 1.372 nossos | +4.719 deles (2.120 funções criadas), 348 rótulos de tabela virtual, 114 comentários onde o nosso nome difere |

As 2.376 funções novas no PS2 vêm assim: 788 do lote deles onde não tínhamos nada, 526 do lote deles onde a nossa
sugestão de confiança 1 era outra, 482 do lote deles que confirma a nossa sugestão, 580 do nome do par no Xbox. Cuidado
com dois grupos: os 75 nomes de par de confiança 1 ficaram só no comentário; e nas bibliotecas da EA a camada de
plataforma é outra em cada console (o som do PS2 vai ao IOP, o do Xbox ao DirectSound), então um nome de par ali pede
conferência (quatro nomes de par dados na primeira rodada, como `FILESYS_size` em 0x252ef8, caíram para par de confiança
1 na segunda e ficaram com o nome e um comentário que o diz).

## 2. A engine de carro

### 2.1 As estruturas e as duas regras de leiaute

`tools/structs_driving.json` (o leiaute do Xbox, MSVC): 280 tipos e 3.202 campos. Os que o porte vai usar logo:
`RigidBody` (0x80, o mesmo leiaute nosso: orientação +0x00, posição +0x10 com o tensor no w, ...), `Simulation` (0xf24
no Xbox: 64 vagas de corpo rígido contra 48 no PS2, donos em +0x2b0 e o estado em +0xe34, como a seção 9 do
`docs/driving.md` já dizia), `PBondCar` (0x420: `tyreTracks` +0x74, `wheels` +0x90, `wheelRoadNormal` +0x190, `wheelPos`
+0x1d0, `suspensionCompression` +0x210, `wheelSlip` +0x220, `wheelSpinAngle` +0x230, `rigidBody` +0x23c, `carControl`
+0x240...), `PhysicsObject` (0x6c), `WWorldPos` (0x40), `WGrid` (0x24: origem, célula, 1/célula, linhas, colunas, nós),
`WCollisionMgr` (0x34), `SMissionManager` (0x1c70, 59 campos: objetivos, mensagens, estágios, 64 listas de regras,
timers, 64 "magic counters", pontuações), `RVehicle` (0x370: instância, zonas de dano +0xe0, o clarão do tiro, rodas),
`RLightManager` (0x3f0: a luz principal, a `specularMatrix` +0x20, as luzes pontuais pendentes).

Para levar um leiaute do Xbox ao PS2 (do relatório deste estudo, conferido no `PhysicsObject`; a conferir caso a caso):
- **o ponteiro da tabela virtual**: o MSVC o põe em +0 e o GCC 2.95 depois dos membros da primeira classe com virtuais;
  numa classe derivada assim, os campos do PS2 ficam 4 bytes antes dos do Xbox (`PhysicsObject`: tipo +0x44, render
  +0x4c, vida +0x58, atributos +0x60 no Xbox; +0x40, +0x48, +0x54, +0x5c no PS2);
- **a entrada virtual**: deslocamento no PS2 = 8 × (entrada do Xbox + 1) + 4 (a tabela do GCC 2.95 tem a `__tf` e
  entradas de 8 bytes; seção 3 do `docs/driving.md`). Com isso as chamadas virtuais que o nosso `ResolveCollision`
  portado faz ao dono ganham nome: +0x1d4 `GetPhysics()` (o `CarPhysics` de 0x100 bytes, campos com o nome do atributo:
  o nosso +0x60 do `InitLevers` é `SPRING_REST_LENGTH`, +0xc0 `SUB_PHYSICS`, +0xc4 `NO_WORLD_COLLISIONS`, +0xc8
  `IS_SNOWMOBILE`, +0xcc `IS_BOAT`), +0x9c `GetCarClass`, +0x1dc `IsReversing`, +0x134 `GetCarControl`, +0x18c
  `GetNumWheelsOnGround`, +0x23c/+0x244 `Get/SetWasInAir`, +0x64 `GetSplinePath`, +0x84 `GetCarType`;
- STL e `std::string` diferem (Dinkumware contra SGI): estruturas com contêineres não batem campo a campo.

### 2.2 Física, mundo e o carro

Tudo isto é código do jogo (vale para o PS2), em C++ no `src/driving/` deles; os endereços são do Xbox (o par do PS2 sai
do `out/xbox/match.tsv`):
- **Colisão entre corpos e com o mundo** (`src/driving/physics/RigidBodyCollide.cpp`): `RigidBody::CollideWithObject`
  0xb0510 (o que falta antes do nosso `ResolveCollision`), `CollideWithWorld` 0xb1420, `ResolveWorldOBBCollision`
  0xaf960 (escala 6000, limites 25 e 5), `CalculateAndApplyWorldDamage` 0xad520, o `CollisionImpact` (0x54: o dono em
  +0x49/+0x4d, a quina que bateu em +0x50); `SimpleRigidBody` (0x40), `Newton`.
- **O mundo** (`src/driving/world/`, `docs/driving/world.md`, `collision.md`): `WCollisionMgr` (0x239a70),
  `GetWorldHeightAtPoint` 0xbf210, `CheckHitWorld` 0xc3b40, `SurfaceBumpHeight` 0xbedd0 (o relevo por seno nas faces de
  tipo 2-10), `WWorldPos::FindClosestFace` 0xd31f0/0xd3050 e `MakeFaceAtPoint` 0xd2f10 (uma face plana de 0,2), `WGrid`
  (`Init` 0xc5fc0), gatilhos, a rede de estradas e o `WRoadNav`, as cortinas.
- **O Vanquish deixa de estar travado** (seção 11 do `docs/driving.md`, "O Vanquish"): as medidas que o
  `PBondCar::PBondCar` recebe vêm de `RSceneObj::GetBoundingDimensions` 0x8dfd0 (a caixa do `BaseDesc` do CARP), os pontos
  de colisão de `PhysicsObject::GetCollisionGeometry` 0x6f4d0 (a `collisionGeometry` do `BaseDesc`), a massa do atributo
  `MASS`.
- **O carro** (`src/driving/game/BondCar*.cpp`; `PBondCar` 0x420, tabela virtual 0x18f580 com 83 entradas):
  `ProcessPhysics` 0x67bd0, `AddWheelForces` 0x670e0, `ProcessSimplePhysics` (a IA) 0x69710, `GetControllerInput`
  0x66640, a moto de neve 0x63ad0, o submarino 0x644c0, a spline 0x65740, `Simulate` 0x6a840, o construtor 0x6d350; o
  ajuste `BondCar_*` vem dos registros "Physics:Physical" e "Physics:Friction" (`InitializeBondCarGlobals` 0x61ae0),
  com as 19 "soft zones"; `PHelicopter`.
- **Câmeras** (226 funções, `RPlayerCamera` 0x370 em todos os modos, as tabelas do `camera.ini`, `RDirectorQueue`),
  **animação** (os `Act*`, 341 funções, e as tabelas de animação procedural), **entrada** (`docs/driving/input.md`: o
  formato dos `.def`; o `Master.def` com 8 pontos de vista e 1 "Driving"; a `ActionQueue`, um anel de 200 ações; o
  `IOModule` é uma tarefa síncrona; o `IFeedback`), **atributos e ajuste** (os `.atr` lidos em três passadas com os
  sufixos `.b .i .u .f .v .m .s .r`; `DTuningDBMgr::LoadDatabase("X:Y", pista)` lê `data\tuning\X\Y\<pista|default>.tun`).

A aritmética deles é a do MSVC/x87 e o código é a versão de Xbox: é oráculo do algoritmo, não dos bits, como a seção 9
do `docs/driving.md` já tratava o Xbox. Portar do MIPS e conferir no `nf_driving_check` continua sendo o caminho.

Pequenas divergências de leitura (nada de comportamento):
- o nosso `WWorldPos` +0x30 se chama `face` (um ponteiro); no PS2 é uma palavra (`lw`/`sw` em
  `TempGetHeightInformation` 0x1f6ae0, que grava 1 nela no recuo) e eles a chamam de `faceValid`: a face em si são os
  três vértices em +0x00..+0x2f. O nome deles descreve melhor;
- a seção 9 do `docs/driving.md` escreve o tensor inverso no mundo do Xbox como R · diag · Rᵀ e a seção 11 (o PS2) como
  Rᵀ diag R: é a mesma conta em convenções de vetor diferentes (linha × coluna), não um erro de leitura;
- o "massa / 8 × 0,05" do atrito na seção 11: o 8 é o valor padrão de `BASE_FRICTION_MASS` ("Physics:Physical").

### 2.3 Os formatos (CARP, mundo, estradas)

Do `src/driving/data/Carp.cpp`, `world/CollisionTypes.h`, `Grid.h` e `docs/driving/core-data.md`, `filesys.md` deles
(código do jogo: vale para os `.crp` do PS2, que são o mesmo UGroup):
- **o resolvedor**: a referência `rs` simbólica e 20 resolvedores por etiqueta (`Wmap`, `wn`, `ni`, `Cams`, `di`, `ps`,
  `ci`, `Trgr`, `Rule`, `AIEl`, `AISp`, `MSet`, `AICo`, `Base`, `pD`, `as`, `an`, `aq`, `el`, `ef`);
- **`BaseDesc`** (100 bytes: modelo, atributos, caixa, caixa de dano, geometria de colisão);
- **`Instance`** (64 bytes): os w das linhas são bandeiras, tipo e índice da animação procedural, o `BaseDesc` e as
  medidas empacotadas, ou seja, as "palavras ainda não lidas das instâncias" da seção 10 do `docs/driving.md`;
- **`CDat`**: `CGrd` (origem, célula, linhas, colunas), `cn` os nós da grade (os 2.111 `cn` de Paris), `ci` a instância
  de colisão (0x40), `ca` o artigo (cabeçalho de 0x20, esferas das faixas, vértices de 16 bytes com contagem, bandeiras e
  etiqueta no w, barreiras de 0x20, grupos de janela), `co` o objeto (0x30, cilindro ou caixa), `de` os elementos
  dinâmicos;
- **`RNgp`**: `RNhd` (0x10), `rs` (0x64), `rn` (0x20), `ri` (0x30), `rj` (0x10), `rr` (0x30); **`Trgr`** (`WTrigger`,
  0x40); **`VisC`** (a cortina, 0x20; no máximo 63 ativas);
- **BIGF**: três assinaturas (C0FB, BIGF, BIG?) e o rodapé "L231"; a busca tenta o arquivo solto primeiro e depois os
  BIGF na ordem de registro (então o `misc.viv` vence); o nome do `.viv` da missão é o que vem depois do último `_` da
  pista (`GameLoop_StartUsingMainBigFile` 0x5a650), e ele é fechado antes do laço;
- **`.atr`, `.tun`, `.def`, `.ini` (DAFI) e `.sub`**: a semântica completa; o **LOCH** (`docs/driving/eagl.md` 2.12).
  As variantes de Xbox das texturas (SHPX) e das fontes (FNTX) não valem para o PS2; as galerias `.gal` eles não fizeram.

### 2.4 O render

Os tradutores deles (NV2A → HLSL, `src/common/gfx/nv2aPixelShader.cpp` e o de vértice) são emulação genérica do
hardware do Xbox: não dizem nada por render method e não se transferem (o PS2 roda microprogramas da VU1 e o GS). O que
vale é o **código do jogo acima do EAGL** que eles portaram (~560 funções de render) e que o nosso protótipo ainda supõe
(seção 10 do `docs/driving.md`, "Divergências documentadas"):

| O que o port supõe hoje | O que o jogo faz (Xbox, código do jogo) | No PS2 |
|---|---|---|
| a `SpecularMat` é a rotação da câmera | `RLightManager::AddSpecularLight` (0x7e950): o referencial com z = norm(L0 − a frente da câmera) (o vetor médio), x = unit(cima × z), y = unit(z × x), transposto; chamado por `SendPerViewPort`; o vetor também vai para a 4ª coluna do bloco de direções | **conferido**: o `AddSpecularLight` do PS2 (0x1aefb8, `out/decomp_driving_render/`) faz a mesma conta (normaliza a luz menos o vetor da câmera, grava em +0xc/+0x1c/+0x2c, monta o referencial com (0, 1, 0)) |
| duas luzes direcionais (`Light1`, `Light2`) e o ambiente | quatro: L1, L2, o chão ("Ambient Ground", direção forçada a (0, 1, 0)) e o céu ("Ambient Sky"); `SetSurfaceProperties`: céu e chão × ambiente, L1 × difusa, L2 sem escala | a conferir (`RLightManager` 0x1aeba8..., `CopyPositionalLightsToPlatformSpecific` 0x1aedc8) |
| o brilho do carro fixo | `SendPerCarInstance` (0x7e310): o mapa de luz da pista (`%s_S`, bilinear, (v − 1)/14 × "Light map lighting bias"; sem mapa, 0,7 × bias), `SetSurfaceProperties(b, b)`, `specularStrength` = 0,7 + 0,3·b | a conferir |
| `ReflectionInfo2` = valores do `CarRender\default.tun` | (Fresnel, Warp, SpecularSpot, SpecularStrength): fresnel 0,3, o warp **forçado a 0,485 depois do ajuste**, spot 3, força 1 (depois a do carro) | a conferir |
| o `ReflectionMap2` é o `bref` estático | `GAME::ReflectionMap*` e `SphereMap` são a textura feita a cada quadro (no Xbox, duas vistas de 256² dobradas numa esfera; o PS2, pelo `RReflection::Draw` 0x1c1ec0, desenha uma vista espelhada de 256²); o `bref` é o `EAGL::EnvironmentMap`; `SpecularMap` = `carS`, `WeaponReflectionMap` = `wref` | parcialmente lido (`Draw_001c1ec0.c`) |
| `GAME::SwitchData` sem índice = a entrada 0 (`car_model.cpp`) | `RCARPFile::RegisterSymbols` (0x7b020): `SwitchData[n]` = base + 16n; **sem índice, a entrada 8**; quem preenche `SwitchData`/`DamageZones` é `RVehicle::SetPerObjectBuffers` (0x95ff0) | a conferir no `RegisterSymbols` do PS2 (0x1a8930; o nosso `driving_sym.py` o tinha posto em 0x1a88e8, uma das disputas) |
| a névoa da família pelo `.tun` | `RFog` ("Render:Fog": início, fim, densidade, modo, cor); o `RReflection::Draw` do PS2 grava FogData = {24560, 0, ?, 4080/(início − fim)} | a conferir contra a VU 0x48 |

E o resto, para as fases seguintes (código do jogo, a conferir no PS2 caso a caso):
- **a ordem do quadro**: `RRenderHigh::Render` (0x8bf30): o reflexo (`UpdateMaps`), a sombra do carro do jogador,
  `StartFrame`, cada vista, `GHud_Render`, `Fog::UpdateScale`, as partículas de veículo, `FinishLights`, `EndFrame`; e
  `RRenderWorldCamera::DoRender` (0x8cfa0): o recorte (2D e cortinas), `SendPerViewPort`, o mundo estático (céu +
  passadas 3-2), os atores, `RenderAllNormal`, as marcas de pneu, carros e objetos adiados e translúcidos, a passada 1,
  as armas dos atores, a passada 0 (do longe ao perto, sem gravar Z), a mira, `DrawEffects` (rastros, vidros, decals,
  raios, GFX, detritos, glares, partículas, clarões), os atores translúcidos, o pós (lens flare, colorize, gain,
  sniper, brilho, câmera do míssil) e o farol;
- **as glares** (as luzes do carro e das lâmpadas; `RGlareManager`, `src/driving/render/RGlareManager.cpp`; no PS2
  `RGlare_PS2.obj`): o banco "Render:Glare" com 80 tipos (halo e raios, textura, giro, cor, tamanho, Z bias 2,0 ou 0,3),
  o atlas `flar`, o piscar (dente de serra, triângulo, pulso), `AddModelGlare` (some com distância/intensidade > 1,6;
  direcional pela frente), `DrawGlares` (dois quads virados para a câmera, mistura ADD, sem gravar Z); as glares do carro
  (1 faróis, 2 freio, 3/4 setas, 5 ré, por `TriggerFX`). Não dependem de teste de visibilidade; o lens flare é só o sol;
- **a sombra** (`RShadowMap`, a mesma API no PS2 0x1eb7d8...): alvo de 512² ortográfico, a geometria do `RVehicle` com o
  `BondCarShadow` (cinza 0,5: por isso nenhuma instância usa esse método no nosso censo), a matriz da textura, as
  constantes `GAME::Shadow*`; só o carro do jogador; e a sombra simples (`cshd` sob as rodas);
- **céu, água e neblina**: `RSky::Draw` (sem neblina nem luzes pontuais, as instâncias do céu levadas ao olho a cada
  quadro, o sol do lens flare a 1500 na direção de "Light1 Angle Y"); `RWater` (4 camadas, `GAME::UVScale1..4`/
  `UVScroll1..4`, `WaterRotToSun` 157°, `CausticControl`, `SwayData` das algas; o PS2 tem um `RWater::Update` a mais);
  `RFog`;
- **decals** (`RDecalManager`: anel de 168 quads, 8 tipos do `decal.dat`), `RLightning`, `RColorize` (o borrão de
  movimento, o infravermelho), `RParticleSystem` (no PS2 `RParticle_PS2.obj`/`PartMaticSprites`), a câmera (perto 0,17,
  longe 3000);
- **os métodos que faltam, por papel** (o vidro, o switch, a sombra): os combiners do Xbox confirmam a estrutura (o
  vidro: base e mapa de dano misturados pelo dano, × cor, + reflexo e especular pelo alfa da cor; o `BondCarTextureSwitch`
  tem o combiner do `BondTexture`, então o "acender" é todo do programa de vértice, como a nossa leitura "família + um
  passo"), mas os números vêm do microprograma do PS2. **43 nomes de método** são comuns aos dois conjuntos (contados
  pelos símbolos `__EAGLMicroCode*` dos `eaglrm.o`/`bondrm.o`: 128 no Xbox, 108 no PS2), não os ~17 que a seção 9 do
  `docs/driving.md` dizia; dos nossos que faltam, `BondCarShadow`, `BondKelp` e `BondUnderWater(Packed)` têm o mesmo
  nome, os outros só um par por papel (`MatCarGlass(Packed)` ↔ `BondCarGlass`, `MatCarTextureSwitchPacked` ↔
  `BondCarTextureSwitch`, `MatBondWater` ↔ `BondWater`, `MatBondTexture` ↔ `BondTexture`...).

Lições dos erros que eles corrigiram no render: a ordem das metades das luzes pontuais (posições, depois cores) confirma
o nosso leiaute da VU 0x40/0x44; uma textura sem cor (A8) deve amostrar branco; o GS também não tem "quads" (as glares
são sprites ou faixas no PS2); o `GGirl` do menu de pausa (`InitGirl`/`DoGirl`, uma textura de 128² decodificada por RLE
com uma tabela de fogo) no PS2 tem um `GGirl::DownloadTexture` explícito: o porte reenviará a textura quando o jogo a
chamar; o que conta pixels (lens flare, tamanho das glares, os alvos de 128², 256², 512²) tem de ser escalado com a
resolução.

### 2.5 O laço, o Scheduler e o relógio

Código do jogo, de `src/driving/engine/GameLoop.cpp`, `src/driving/Scheduler.cpp`, `EventManager.cpp` deles:
- **o `main` e a carga**: `GameMain` 0x5a1b0 e `ApplicationMemoryHeapConfig` 0x59920 (a página de troca, "LOADER READY",
  o modo de vídeo, `TIMER_init` na taxa do vídeo, `UMemory::Init(0x2400000)`); `Bond_StartUpSystem` 0x59d80 (textos,
  ajuste, atributos, FILESYS, `misc.viv`, os materiais do EAGL, a tela de carga, o `.viv` da missão,
  `InitRigidBodySystem`, `LoadTrackFile`, os controles); `GameLoop_StartUp` 0x5b0d0 (RayShell, `Simulation_Reset`,
  `RealClock_Init`, `EventManager`, `Scheduler`, os veículos, o som (o `.spe` volta ao inglês se faltar), os atores,
  `SWeaponManager`, `World::Open`, a IA, as estradas; o carro nasce na altura do chão sob a origem + 1); `RunTheGame`
  0x5aa80; `GameLoop_CleanUp` 0x5a870. A seção 3 do `docs/driving.md` dizia que o corpo do `GameLoop_StartUp` não tinha
  sido lido;
- **o `Scheduler`** (`Run` 0x5ba80): quatro agendas (por volta do laço, no ritmo da simulação, a meio ritmo `tick & 1`,
  a um quarto `tick & 3`), cada uma com 8 prioridades. As tarefas de `RunTheGame`: na simulação `ESimFrameUpdate`
  (prioridade 0), `EAnimUpdate`, `EAIUpdate` e `ECameraUpdate` (2), `ESimEndFrame` (7); por volta `ERenderFrame` (6) e
  `EAudioUpdate` (7). O passo: dt = (`Clock` − último) × `timeScale`, truncado; 0 não faz nada; abaixo de 12 roda tick a
  tick, desenha só no último e roda `EventManager::RunEvents` depois de cada tick; **com 12 ou mais, joga o tempo fora**;
  há `oneTickPerRun` e o pulo das cinemáticas (desenha 1 tick em 4). **Bug do original**: a fração do `timeScale` se
  perde (0,75 roda a 0,5; 1,5 roda a 1); eles corrigiram, nós devemos preservar;
- **os eventos**: um buffer circular de 32 KB ("EventBuffer"), eventos alinhados a 16 bytes, o trabalho no destrutor (o
  que a seção 3 do `docs/driving.md` já tinha visto), sem checagem de estouro;
- **o relógio**: `RealClock_InterruptHandler` (0x5b9f0) soma o `Clock` por uma tarefa do `TIMER` na taxa do vídeo; o
  passo da simulação é fixo (`SimTimeStep`, a mesma `DAT_00234e30` da seção 9; 0x3c888889 no Xbox, 0x3c888888 truncado
  no PS2). Os 50 Hz que eles citam são do Xbox europeu (o jogo deles corre a 5/6 da velocidade do NTSC);
  `ActManager::StartUp(1/taxa)`: a animação anda na taxa do vídeo;
- **o fim**: `SMissionManager::Win` (0xb8574) lê `CHAIN_NEXT_MISSION`/`CHAIN_NEXT_MISSION_NAME` dos atributos da pista (as
  cadeias também estão no nosso ELF, em 0x290508) e encadeia as partes da Ilha; o `main` acerta o resultado (como a
  seção 4 do `docs/driving.md`), restaura a dificuldade de +0x624 e chama `ReturnToAction` (0x130740). O "super fácil"
  (`gSuperEasy`) só vale em Paris, na primeira execução, com a dificuldade 0 e +0xa18 = 0.

### 2.6 A página de troca

O `LaunchPage` deles (`src/driving/engine/GameLoop.h`, 0xa4c bytes; o `psiLaunch.bin` tem 0xC00) tem o leiaute do nosso
bloco UberBond (0xA50 no PS2: 0xa4c arredondado a 16), conferido campo a campo contra o `Boot_GetPTPData` do PS2. O que
eles sabem e nós não (do lado do `DRIVING`): +0x5d8 (libera o outro filme depois de uma falha), +0x5dc `driveCount`,
+0x614 (2 se +0x970), +0x618 `bootCount`, +0x624 `savedDifficulty` (os quatro dentro da nossa área +0x504..+0x630), e
**+0xa14 = `PlayerSetting[0]` +0x10**, o estilo de controle do carro (conferido no PS2: `Boot_GetPTPData` 0x1c8198 grava
`PTPDATA`+0xa14 de `PlayerSetting`+0x10 e `Boot_LoadPTPData` 0x1c8560 o devolve; no nosso `input.h` o campo ainda é
`unknown_10`). O que nós sabemos e eles não: a metade do `ACTION` (o codinome, as opções, `MPSettings`, `def_codename`,
os bits de bônus, as tabelas de pontuação +0x154/+0x184), +0x970 (tela larga), os valores do resultado +0x978 (0 a 4),
o bit 8 de +0x9a0 (ouro em Paris), +0xa30/+0xa34 (a posição da tela), +0xa3c (a melhor medalha), +0xa48 (a troca
automática de arma). A missão 8 (`snow2a_race`): o README deles diz que uma trapaça na missão 6 chega a ela; não mostram
a prova (a conferir no `.atr` do MIS4).

### 2.7 Som e filmes

- **A biblioteca SND da EA** (`src/driving/sound/snd/`, 305 funções, 11,5 mil linhas): as camadas de cima são o mesmo
  fonte nos dois consoles (a API, o gerente de 224 vozes, os bancos `BNKl` v5, as etiquetas PT, `SNDSTRM`/`STREAM`, o
  mixador `MIX`/`SFILTER`); a de baixo é outra (no Xbox 180 buffers do DirectSound e seis anéis 5.1; no PS2 o
  `SNDDRV.IRX` no IOP). O decodificador **EA-XA** (`decodexac` 0x149d80: os coeficientes, blocos de 15 bytes com 28
  amostras, a ordem das contas) é o que falta ao nosso SPU2 para os fluxos `.asf`. O framework A* do jogo: a regra de
  nome do `AIndex` ("WPN_GUN_FIRE" vira "SFX_FIRE"). A música é a `AStream` "music" com uma fila de eventos e uma máquina
  de "hold", disparada pelos eventos `E*` (que eles não portaram). Um banco do PS2 (`MIS01.VIV:vanquish.bnk`) não tem as
  etiquetas PT 0xa0 (codec) e 0x8c: usa o padrão da plataforma (provavelmente PS-ADPCM, a conferir), 0x84 ≈ 24 kHz.
- **Lição para o porte**: no jogo o fim de uma fala (`SNDover`, `SNDSTRM_status`) pode disparar eventos de missão, e o
  filme se ritma pelo consumo do áudio: o áudio tem de ser consumido de forma determinística (como o nosso SPU2 já faz).
- **Filmes** (`docs/driving-fmv.md`): os `.mad` do Xbox (vídeo MAD a 25 fps, EA-XA 5.1) não valem para os `.mpc` do PS2
  (MPEG-2); vale o lado do jogo: `PlayMPC` (0x130780..0x130b50) é **bloqueante**, chamado de `FUN_0005a6b0` (com a
  legenda) e de `ESetVideo::~ESetVideo` 0x44970; o volume = efeitos × 127/100; o relógio do filme em milissegundos puxado
  para a posição do áudio (correção dura acima de 264 ms); o pulo com A/B/Start lidos direto do controle; as legendas
  `.sub` por número de quadro.

### 2.8 Os travamentos que eles corrigiram

Quase todos são do carregador ou do Xbox (seções críticas estáticas, um ordinal do kernel errado, FS:[0x04]/[0x20],
`WBINVD`, DEP, o alias físico 0x80000000, `KeTickCount` parado). Os que dizem algo sobre o jogo:
- o esgotamento do pool de buffers do DirectSound (`docs/driving-engine-plan.md` seção 4, `SNDPLATFORM_playtimbre`
  0x142b10) é uma falha latente do back end de Xbox da EA (com os dois pools vazios ele lê o índice −1), que só aparecia
  quando o status relatado mentia: no PS2 o back end é outro, mas a recuperação de voz também depende do status;
- o destrutor do `IFeedback` espera a thread dele no fim de cada parte de missão; no PS2 o `IFeedback` também tem thread
  (pilha estática de 0x2000): o porte precisa reproduzir isso ou chamar o `Update` por tick;
- lições de porte deles: o MSVC transforma um desvio em `fabs` e perde o −0; um `bool` só escreve AL; um `ResetDamage`
  que saiu com 175 de vida em vez de 700; um resolvedor de `Rule` que lia a tabela errada e derrubava as missões 1 e 6.

## 3. A engine a pé

O `default.xbe` é o código da Eurocom compilado pelo MSVC; o nosso `src/engine` já porta 79% dos bytes do `ACTION.ELF`.
Comparamos os docs e as funções reescritas deles (`docs/collision`, `docs/effects`, `docs/weapons.md`, `docs/drone`,
`docs/ui`, `docs/savestate`, `docs/switch-channels.md`, `docs/global-clears.md`, `docs/architecture`,
`docs/function-coverage.md`, `docs/gamecube-checks.md`; `src/action/`) com os nossos portes, e cada divergência foi
conferida no código do PS2 (`out/decomp`, `out/disasm`, ou a desmontagem do `ACTION.ELF`). Os endereços deles são do
Xbox e os do `ACTION.ELF` deles são do europeu: tudo foi casado pelo nome; os endereços abaixo são do nosso NTSC-U.

### 3.1 Erros e lacunas nossos (conferidos no PS2; nada corrigido neste estudo)

Com efeito no jogo:

| O quê | O PS2 | O nosso | Efeito |
|---|---|---|---|
| **`ReadTuningVars`** é um stub | 0x1d3a80 (0xe00 bytes), chamada pelo `ResetMap_Load` a cada carga. Grava sem condição (0x1d3b5c-0x1d3c1c): `Plr_AimSpeed_X/Y` = 0,1275 e 0,136, `Autoaim_Angle_H/V` = 0,12 e 0,22 rad, `Autoaim_Range` = 25, `Autoaim_LockOnMul` = 1,4, `Autoaim_EasyMul` = 1,9 (conferido na desmontagem: as constantes montadas com `lui`/`ori` em f2..f17 e os `swc1` em 0x30cd0c...); depois um `switch` pela fase (0x1d3c20) com 8 blocos (0x1d3cec-0x1d4874) de `DroneDamage_*`, `DroneArmour_*`, `DroneFiring_*`, `DroneCaptain_*` e `Plr_DMod_*` | `NF_STUB(ReadTuningVars)` em `src/engine/modules_stubs.cpp:33`, chamado em `resetmap.cpp:405`: ficam os valores do `.data` (`player.cpp:72`, `player_weapon.cpp:51-58`: cones de 10 graus, alcance 35, lock-on × 2; `DroneDamage_*` pelo `static_init.cpp`) | em todas as fases: a mira automática e a velocidade da mira, o dano que o jogador recebe (07000012: 0,75/1/1,25 por dificuldade no nosso, 0,5/0,6/1,0 no PS2), o tiro na cabeça do inimigo (× 100 contra × 10), o capacete (0,5 contra 1,0), a precisão dos inimigos (0,5/1/2 contra 0,7/0,8/1,0). As medições do `CLAUDE.md` que dependem disso (a morte em ~9 s em 07000012, `tiros_ia`, os acertos da mira automática) mudam quando for portada. Os valores por fase são os do `ReadTuningVars`/`SetStandardDroneTuning` deles (`src/action/game/obj/Player.cpp`), que batem com os stores do PS2; as fases: Henderson 07000001-04, Castle 05-08, Tower 09-0b, PowerStation 0c-0d, Tower2 11-13 e 4a, EvilBase 14-17, SpaceStation 1b, e o último bloco as arenas |
| **`NDrone2_NavNodeCache`** é um stub | 0x155cd8 (108 bytes), a cada tick pelo `Drone_InitComms`: monta `NPCGlobals.player_target` (os pés do jogador em +0x1d0, a sala em +0x1e0, a máscara 0x1000000 em +0x1c0, o objeto em +0x1c4) e chama `AINetwork_BuildAITarget` (0x15ac78) | `src/engine/drone_stubs.cpp:13` | a rota A* até o jogador nunca sai (`AINetwork_SetupRouteToObject` aponta para `player_target`, cujo `frame` fica 0: `DoAStarPath` devolve 4), só a ida em linha reta; `DSTATE_DroneStuck` espera o `frame` avançar e não sai (`docs/drone/bots-and-navigation/README.md` §3.1, §4.3 deles) |
| **`DroneFunc_HandleExplosives`** é um stub | 0x1435f0 (428 bytes): o primeiro ponto de atenção de tipo 2 no raio, com as propriedades 0x3b/0x3a, visibilidade >= 0,7 e linha de visão, recebe a mensagem 0x21 | `drone_stubs.cpp:12`, chamado em `drone.cpp:989` | ninguém foge de granada: `DroneFunc_ConsiderExplosive` e os estados 0x86-0x88 ficam inalcançáveis (`docs/drone/architecture/README.md` §2.4 deles) |
| **`Drone_MessageObjVicinity`** é um stub | 0x13a438 (320 bytes): cada drone a até `radius` do objeto (com o teste de visão pelo osso 5 quando a mensagem é 0x18) recebe a mensagem | `modules_stubs.cpp:65`, chamado por `bullet.cpp:546` (0x18, 20 m) | a granada de clarão não atordoa ninguém (os estados `StunGrenade*` ficam mortos) |
| **`NDrone2_ControlDTYPE_Zoe`** fora do `Drone_ResolveCode` | 0x14b058 (156 bytes): com o canal 1, liga o canal 0x9b; com 0x9b e o jogador a mais de 10 m, `DroneFunc_SetMissionFailReason(drone, 0xf)` (o canal 99) | `drone_tables.cpp` não a tem: o controle do tipo fica nulo e `Drone_Control` usa o padrão | a Zoe de 07000008 não liga 0x9b nem falha a missão quando o jogador a abandona. O do ninja só perde os olhos (`NDrone2_CreateNinjaEyes`, também stub); o do astronauta não perde nada |
| **`Drone_FellOutMap`** é um stub | 0x13a878 (92 bytes): fora do multijogador, `NDrone2_Enable(0)` e obj+0xfe \|= 1 (apagar) | `modules_stubs.cpp:55`, chamado em `control.cpp:813/861` | um drone que cai fora do mapa continua existindo |
| **`Rand_FRand_MVar2_Vec`** é um stub | 0x1e00d0 (464 bytes): soma a·r − b a x, y e z, avançando os sorteios três vezes | `modules_stubs.cpp:44`, chamado pelo `Camera_Update` (`view.cpp:493`) com o tremor da câmera | o tremor das explosões e do terremoto nunca move a câmera, e a sequência do `Rand` perde três sorteios por tick de tremor (tudo o que depende do `Rand` diverge do PS2 depois de um tremor) |
| **`psiDecompressWoman`** é um stub | 0x1202a8 (608 bytes), chamada na pausa: um tick sim, um não, decodifica um RLE de 128×128 (trechos zerados e trechos da textura de fogo 0x030000da, que rola com `WomanFrame`) e o converte (`Conv8to32`) para a textura **0x030000db**; volta ao início no quadro 348. Os dados vêm do item tipo 16 do pacote (`PS2SetUpWoman`, já portada) | `src/platform/psi_stubs.cpp:50`, chamado em `game.cpp:56` | o fundo da pausa é uma animação, não uma imagem parada: o `CLAUDE.md` (lacuna 6) e o `docs/port.md` descrevem a textura 0x030000db como arte fixa ("luzes de festa, um homem com uma taça"), e o `hd_ui/map.json` a troca por um SVG fixo. A versão deles: `src/action/engine/Woman.cpp` (no Xbox o último quadro fica 240 ticks; no PS2 não) |
| **`View_DrawGlist`** é um stub | 0x1e77f8 (76 bytes): `View_RotTransScaleMatrix` + `psiDrawObjectMatrix` | `modules_stubs.cpp:97`; o único chamador é a mira laser dos personagens com a arma 0x18 (`anim.cpp:2603`) | esse raio laser nunca é desenhado |
| **`Input_Ready`** é um stub | 0x17f1c0 (60 bytes): `FreezeGame` = 0, `Input_ClearAllActions(-1)`, `DList_Init(ComboKeysList, 0x18, 10)`, `psiInputReady()` | `modules_stubs.cpp:32`, chamado em `resetmap.cpp:80` e `game.cpp:120` | a lista das combinações (`ComboKeysList`, a "InputEventList" deles em `src/action/input.cpp`) nunca é criada (a lacuna das combinações de trapaça do `CLAUDE.md`) |
| **`Occlude_Create`** é um stub | 0x17ea80 (284 bytes) | `modules_stubs.cpp:61`, chamado em `bullet.cpp:562` | a nuvem da granada de fumaça não bloqueia a visão da IA |
| **`Mission_MonitorObjectives`**: o "todos cumpridos" | 0x1827d8: o acumulador é um AND com o **byte cru** do canal (0x1828b4 `lbu a1`, 0x1828c0 `and a0, a0, a1`): um canal com valor par (2, 4...) zera o acumulador | `mission.cpp:174-175`: `switch_channels[...] != 0` | latente: só aparece se um canal guardar um valor par (o evento 17 dos scripts, `script.cpp:612`, grava o byte do disco; não conferido se algum script o faz). O deles tem o mesmo desvio |

**Situação (12/10/2026):** portadas do MIPS do PS2, com testes, todas as linhas da tabela menos `psiDecompressWoman`
(deixada para quando o renderizador puder trocar uma textura a cada quadro): `ReadTuningVars` (`src/engine/
resetmap.cpp`; os oito blocos por fase conferidos no interpretador MIPS contra o código do ELF, 83 códigos de fase,
`ajuste das fases` no `nf_loader_check`), `NDrone2_NavNodeCache` (`ainetwork.cpp`), `DroneFunc_HandleExplosives`
(`drone_behaviour.cpp`), `Drone_MessageObjVicinity` e `Drone_FellOutMap` (`drone_func.cpp`), os controles da Zoe, dos
ninjas e dos astronautas (`drone.cpp`, no `Drone_ResolveCode`), `Rand_FRand_MVar2_Vec` e `View_DrawGlist` (`view.cpp`),
`Occlude_Create/Update` (`bullet.cpp`), `Input_Ready` e a `ComboKeysList` (`input.cpp`) e o AND com o byte cru do
`Mission_MonitorObjectives`. Também conferidos e corrigidos no código: o material 0x10 é o GLASS, as bandeiras 4/8 do
passo são os dois pés (osso 55 e 51), as bandeiras de arma 0x10, 0x400 e 0x20000 (o detonador), o parâmetro 13 do
drone (`Drone_tag::death_script`) e o tempo do atordoamento da arma 0x35 (30 s). O efeito medido no jogo está no
`CLAUDE.md` (Status, "Per-level tuning and the on-foot gaps").

**Situação (13/10/2026):** o `psiDecompressWoman` também, com a troca de textura nos renderizadores
(`Renderer::update_texture`, Direct3D 11 e Metal): `src/platform/psi_woman.cpp`, o `PS2SetUpWoman` e o trecho do
`CreateSpaceAndPackTextures` que dá à 0x030000db a paleta do fogo e a zera na carga (o desenho guardado no pacote, as
luzes de festa e o homem com a taça, nunca aparece no PS2); a camada HD deixou de trocá-la pelo SVG fixo. O que o
MIPS do PS2 mostrou além do que está na tabela: o `skipit.288` conta `PS2FramesToSkip` (a 30 quadros, um quadro do RLE
por tick); o espaço do fogo fica linear porque o `CreateSpaceAndPackTextures` pula o `Conv8to32` dele, e o fogo de 128 x
256 ocupa o espaço inteiro; o RLE é o mesmo nas 33 fases (88.638 bytes por volta). O `WIP` deles (`Woman.cpp`) não
decodifica o RLE. O resto em `docs/port.md`, "O fundo animado da pausa".

Latentes ou de leitura (sem efeito hoje):
- `LS_LoadFromBuffer` (0x1ce1c8): o nosso `loadsave.cpp:227` limita a cópia a 0x1000 bytes; o único chamador do PS2,
  `Boot_LoadPTPData`, passa 0x12c0 (a cópia original passa do `InBufData[0]` mas fica dentro do `InBufData`). Rever quando
  a volta do `DRIVING.ELF` for portada; idem `bootup_bootup`, que no PS2 (0x1c7e78) aponta `NewScoresRef` para a `PTPDATA`
  (o nosso devolve nulo, `player_stats.cpp:154`).
- Comentários e nomes errados (o código está certo): `effect.cpp:110` (o material 0x10 é GLASS, não a água: `EffectInfo`
  entrada 16; a água é 13/14); `effect.cpp:187` (as bandeiras 4/8 são o pé esquerdo/direito, não a mão); as bandeiras de
  arma em `player_weapon.h:62-69` e `player_weapon.cpp:2156` (0x20000 é o detonador: −10 no timer dos projéteis vivos da
  classe e a volta à arma base; 0x10 é "vazia: troca para a variante com munição"; 0x400 é o tiro contínuo);
  `AnimDatumGetWeaponInfo` (`anim.cpp:1225-1236`: +0x80 é o modelo da arma na mão dos personagens e nas pickups, +0x60 o
  som do tiro em terceira pessoa); `Swoosh_Create(..., width)` e `trail_width` são uma vida em quadros, não uma largura;
  `bullet.cpp:110-115` (a dispersão sorteia entre s e 2s); na IA, o parâmetro 13 é o script de **morte** (`drone.h:103/487`,
  `enter_script`), o 8 um modo secundário (`channel_state`), `DIVars`+0x30 a celglist (não o dono), o bit 2 do bot+0x734
  "recuperando" (não "congelado"), `drone_combat.cpp:5370` (a arma 0x35 atordoa 30 s, não meio segundo), `drone.h:201`
  (`armour` vale sempre 0); `WWorldPos`+0x30 do carro (seção 2.2).
- Inalcançáveis com os dados do disco: `DroneModeSettings[mode]` sem limite para o modo 0x23 (`drone_init.cpp:134`); o
  `++sm_next_id` em 16 bits (`drone_sm.cpp:76`).
- Os auxiliares do modo de câmera 2 (`camera_track_XZ_pos_behind`, `camera_track_Y_pos`, `Camera_PosYRot`,
  `Camera_PosXRot`) ainda são stubs e o evento 9 dos scripts os alcança (`script_support.cpp:199` → `player.cpp:2214`).
- No `CLAUDE.md`, "Rest of `hashtable_*`" (lacuna 6) está velho: as 14 funções `hashtable_*` do ELF já têm `[decomp]`.

### 3.2 Erros e leituras incompletas deles (o PS2 confirma o nosso)

Para levar a eles, com o endereço do PS2 NTSC-U de cada prova:
- **Missão** (`src/action/game/sp/Mission.cpp`): `Mission_Init` deixa a fase atual no estado 1, o PS2 (0x182270) usa 2
  (os objetivos voltam a 0/1 e o canal ao valor inicial; com 1, recomeçar mantém os objetivos cumpridos);
  `Mission_MonitorObjectives` sai depois de cada falha (o PS2, 0x1827d8, faz as quatro e ainda liga o canal 100);
  `Mission_Update` caso 4 grava em `GameState`+0x48 a fase atual (o PS2, 0x182c60, grava `BaseMap`; a fase atual só no
  multijogador).
- **Canais e cargas**: `docs/switch-channels.md` diz que `Init_SwitchChannels` zera tudo, mas `switch_channels_time`
  recebe o contador de quadros (0x134310) e a função é pulada nas cargas quentes (`ResetMap_GameInit` 0x1d5080);
  `docs/global-clears.md` a lista como zerada.
- **Menus**: `docs/ui/credits.md` (+0x74 do controle é a largura, não o x; o código deles está certo); `P_CNMENU`
  seleciona o item 7 (0x213288), os docs deles dizem 7 e 1.
- **Efeitos e armas**: `docs/effects/data.md` funde o quique (impacto 0x80: reflexão pura e a velocidade × e) e o
  ricochete (0x10: v − (1 + e)(v·n)n, a velocidade intacta) numa fórmula (`Bullet_CollisionHandler` 0x12ce80);
  `Effect_RicochetProb` sai com 0 sem `Rand` quando a máscara é 0 (0x1cf290: muda a ordem do gerador); a dispersão
  acumulada é truncada para inteiro (`Bullet_init` 0x12c5a0); o rascunho de `Break_DoBreak` usa a posição e `<=`, o PS2
  (0x12c168) o centro da esfera e `<`; `docs/collision/functions.md` sobre o fim da coleta de `Collide_PlaceObject`
  (0x1ecfb8); prováveis, sem ver o Xbox: `Light_Update` condiciona o pisca ao som (o PS2 0x1cafb8 a +0x24) e
  `PositionBeam` não grava o centro da esfera (o PS2 0x1a9400 grava `from` em obj+0x80 e o raio).
- **IA**: o +0x130 do Xbox (+0x14c no PS2) é o multiplicador do dano do tiro, não uma velocidade de animação
  (`PunchImpact` 0x145f28, `BulletImpact` 0x1461c8, `NDrone2_DoHitEffects` 0x1458e0); +0x3d é "uma rajada só"
  (`DroneWeap_Fire` 0x17a8c0); `ConsiderAlerted` espera 60 quadros × `FRAME_RATE_DIV` (1 s), não 60 s (0x1763b8);
  `DoBulletAccuracy` usa 0x0c <= fase < 0x0e (0x179740); `CloseCombatImpact` testa o conjunto de animação, não o DTYPE
  (0x1789c0); `Ready2Fire` no modo 4 devolve 1 sem o teste de mira (0x178cfc); o controle da Zoe falha a missão com o
  jogador **longe** e 0x9b/0x63 são canais; `DroneModeSettings` tem 35 entradas (420 bytes em 0x29ad40) e o lado fica em
  +0x44; o A* fecha os nós com 0x40000000, nunca 0x80000000 (`DoAStarPath` 0x1571b0..0x157780; pode ser diferença do
  Xbox); os modelos dos geradores não copiam a sala (ela ainda é nula); o objeto em `NPCGlobals`+0x1a4 é o jogador falso
  da trapaça `Drone_DummyPlayer`.
- **Laço, scripts, objetos** (`docs/architecture/`): o estado 4 do GameFlow é empurrado pelo fim do `ResetMap_Load`
  (0x1d6360: `PopState`, `PushState(4)`; o 4 roda `Locks_Init` e cai no 2, o estado em jogo); o evento 3 dos scripts é
  uma espera (0x1d95b8: `SCRIPTINFO`+0x9c = a0 − 1, 0 espera o `SP_UnPause`), não um salto; o segundo argumento de
  `SFXSetEnvironment` escolhe o volume interno (0 dá 100, o resto 50; `SFX.IRX` 0xdb4) e "debaixo d'água" é o reverb
  −0x6768; o raio do `AnimObjectDraw` é a mira laser da arma 0x18 (0x1c4490), não uma sonda do chão; obj+0xd8 do Xbox
  (+0xfc no PS2) é "desenhado nos últimos 2 ticks" (lido por balas, drones, holofotes); viewer+0xf4 (+0x108 no PS2) é o
  zoom; o `Flicker_Update` do PS2 (0x1ab960) busca a luz por `Light_FindMapLight` e, apagada, solta o gás de tipo 4 um
  tick sim, um não; a visão noturna derruba o alfa do tweak a 1/4 (0x1e5888), não o brilho; o dano de queda soma
  `FRAME_RATE_MUL`, então não depende da taxa; `Car_Create` (0x1305b0): o modo 1 é o tanque, o 2 o helicóptero, o 3
  sorteia (`Rand(100) < 51` dá helicóptero) e qualquer outro valor dá o helicóptero (o padrão deles cria o tanque; pesa
  em 07000046); o "bug" `surface[-1]` que eles veem num raio está, no PS2, no teste da esfera `Intersect_PointGeom`
  (0x1e92d8).

### 3.3 O que eles entendem e nós temos como lacuna

- **O multijogador e os bots** (a lacuna 6 do `CLAUDE.md`). O tamanho no PS2: `BOT_*` 20 funções (7,4 KB),
  `BOTSTATE_*` 34 (13,5 KB), `BOTWEAP_*` 21 (5,4 KB), `NDrone2_DSTATE_Bot*` 55 (19,5 KB), 9 auxiliares só dos bots
  (3,9 KB), `MP_*` 77 (36,6 KB; temos 4), os menus 36 (33 KB; temos 2): uns 120 KB. Deles: os menus `P_MP*`, `C_SBMP*`,
  `C_RBMPSETUP`, `P_MPCONFIRM`, `P_MPDEBRIEFING` reescritos e documentados (`src/action/ui/ui_mp.cpp`,
  `docs/ui/handlers.md`, `items.md`; o `C_MPDBG` é o "debriefing", as colunas dos jogadores, não depuração); 19 funções
  `MP_*` (`src/action/game/mp/multiplayer.cpp`: times, nascimentos, `SortOutWhoWon`, `RestartScenario`) e o fluxo em
  `docs/architecture/player-weapons-mp.md` (os fins de partida pelos canais 0xfd/0xfe, a pontuação em `MP_PlayerKilled`);
  o cérebro dos bots (`docs/drone/bots-and-navigation/README.md` §5: `BotGlobal`, `pickGoal`, `processGoals`,
  personalidades, mira, inventário) e as estruturas `BOT_stats_t`, `BOT_goal_t`, `BOT_vars_t` (`BOT.h`). No PS2 os bots
  são 4 × 0x780 bytes (`BOT_vars` 0x26d660) e o multijogador tem 8 jogadores; no Xbox, 6 × 0x768 e 10.
- **O seletor de filmes `P_FMVTEST`** (`docs/ui/movie-selector.md`: os 22 filmes com os nomes de desenvolvimento): o
  tratador do PS2 (0x20a968) é trivial de portar.
- **A `PTPDATA` tipada** (`sNightFireShared_tag`, 0xa50 bytes) e o `Boot_LoadPTPData` campo a campo
  (`docs/savestate/related.md`): servirá à volta do `DRIVING.ELF` (seção 2.6).
- **Nomes de campos** que no nosso código ainda são `unknown_xx`: `weapon_definition_tag` (`docs/weapons.md`: +0x06
  `botWeaponClass`, +0x30/+0x34 os nomes dos modos de disparo, +0x38/+0x3c os nomes longo e curto, +0x4c o clarão em
  terceira pessoa, +0x50 `weaponDatum`, +0x60 o som em terceira pessoa, +0x7a uma duração, +0x80 o modelo no mundo,
  +0x84 `animSet`; as classes de dano de +0x10: 1 corpo a corpo, 2 laser, 4 atordoar, 8 pistolas, 0x10 energia e armas
  montadas, 0x20 explosivos, 0x40 rifles, 0x80 submetralhadoras, 0x100 aparelhos; as tabelas de bandeiras de +0x68,
  +0x6c, +0x70); a colisão (`docs/collision/data.md`: `HITTEST::unknown_90` é o dano, `HITDATA::unknown_08` o dano,
  `unknown_20` a soma das tangentes, `unknown_52` o osso; as tabelas de bits de máscara e de bandeiras); `obj_tag` +0xd9;
  `HEINFO_tag::decal_foot_b/a` (pé esquerdo e direito); a IA (`docs/drone/behaviours/behaviour_properties.md`: os 91
  nomes das propriedades de comportamento, que usamos por número; os sete `stats` (precisão, agressão, velocidade, ?,
  reação, recuperação, lado ruim); `unknown_29` = `usingSecondBehaviour`; `unknown_15` = voz e animações femininas; os
  enums DSTATE, DMODE, DTYPE e DRONE_MSG em `src/action/game/drone/NDrone2.h`); `PlayerSetting`+0x10 = o estilo de
  controle do carro (seção 2.6).
- **As outras lacunas do `CLAUDE.md`**: `BodyGlow_*` (`docs/effects/functions.md` §2.10, conferido no PS2:
  `BodyGlow_Create` 0x1ac158 cria o tipo 0x46 com o glist 0x200025f, escala 6, bandeiras 0x2408, força 0x100 e 15 membros
  0x200055a; `BodyGlow_Update` 0x1ac2c8 no modo de visão 2, até 10 m, alfa (1 − d/10) × 128 × força / 256; o painel 0x11
  do OICW também o liga); `Car_*` (`src/action/game/obj/car.cpp`: `Create`, `Init`, `Reset`, `InitBits`, `Activate` —
  estado 5, subestado 0xb, câmera 0xd, como o PS2 0x131c48 —, `Deactivate`, `PlayerHasDied`; nem eles têm `Car_Update`
  (4.188 bytes), `Car_SetBits` e `Car_CollisionHandler`); `Bubble_Update` e `Sunreflection_Update` (eles também não acham
  quem as cria, a nossa conclusão); `Corona_Update` (descrita, mas morta no PS2); a tabela dos modos do
  `Player_PositionCamera` (`docs/architecture/collision-physics-camera.md` §3.2) para os `camera_*` que ainda são stubs.
  Sem nada útil: `SqueezeColourIn`, `CreateMoveRG2BA`, `CommonAnimFile` (os docs de animação deles estão num ramo,
  `blender-exports`, fora do clone).

### 3.4 Diferenças entre as versões (Xbox × PS2)

Conferidas do lado do PS2:
- **Tamanhos**: `Drone_tag` 0x978 no Xbox e 0xd20 no PS2 (os `_VECTOR` alinhados a 16; a máquina de estados em +0xec e
  +0x108, health 0x90/0xac, flags 0x3f4/0x4f8), `DIVars` 0xa4/0xc0, `HITTEST` 0x8c/0xa0, `HITDATA` 0x50/0x60, `BLData`
  0x8fc/0x970, `SCRIPTINFO` 0xab0/0xac0, `PlrMissionStats` 10 × 0x38 / 8 × 0x38, `NPCGlobals` 0x142c/0x1d10,
  `MPSettings` 0x23c/0x1dc, os bots 6 × 0x768 / 4 × 0x780.
- **O save**: o bloco GSET do PS2 (0x1cd9d0/0x1cdbc8) grava a posição da tela e 219 palavras reservadas (~905 bytes) e a
  carga aplica o modo do som e a tela larga; o do Xbox, 7 palavras (49 bytes); `LS_Load` pede 0x1000 bytes no PS2.
- **Os menus**: as variantes de plataforma dos registros do arquivo de menu são 0 e 3 no PS2 (`MenuManager_Create`
  0x21b908), 0 e 1 no Xbox; `C_SBSCREEN`, `P_SCREENADJUST` e `P_FMV` são vazios no Xbox; o `C_KEYBOARD` do Xbox confere se
  o codinome já existe, o do PS2 (0x213a98) não; `P_CREDITS` grava x e largura no PS2, só a largura no Xbox; no PS2 a
  missão de carro sai do mapa por `level_chosen` e a contagem de `P_NFMAP` (0x20b8d8).
- **Colisão**: no PS2 o raio e a cápsula são as versões VU0 "ASH" (0x10a8c8, 0x109da0), com a normal × 1/16384 (no Xbox
  1/16383), sem o pré-filtro `surface & 0xc0` do raio e sem o pré-teste da cápsula do Xbox.
- **Outros**: as coronas do PS2 são do tipo 1 sem atualização (ninguém chama `Corona_Update` 0x18da78; no Xbox, tipo 69
  com fade por linha de visão); o `fireRate` é um quadro menor no Xbox nas armas 2-12, 17, 65, 66, 69 (`docs/weapons.md`);
  12 estados da IA que o linker do Xbox fundiu são funções separadas no PS2; `NDrone2_CheckSurrender` em linha dentro de
  `DroneFunc_FirstSightState` no PS2; os alertas de grupo (0x11/0x12/0x13/0x15) não fazem nada nos dois, por caminhos
  diferentes (no PS2, `DroneVision_ShoutFromOtherDrone` 0x177a30 zera o remetente); `DroneFunc_RecoverTime` em outra
  ordem de contas; filmes XMV (25 `.xmv`, WMV2, 640×480, 25 fps) e o save em `u:\` no Xbox.
- **Quadro e apresentação**: o Xbox chama `GS_SetRefreshRate` a cada tick com a mesma taxa, o PS2 (`main` 0x109080) pula
  1 ou 2 VBlanks e cai para 30 Hz sob carga; a câmera 9 só existe no Xbox (o `Camera_CreateCameras` do PS2, 0x1bb2b8,
  cria 0, 5, 7, 4, 6, 8 e 10); a névoa do Xbox é uma tabela de 18 fases com a névoa linear do D3D, a do PS2 a passada por
  Z de 7 fases decidida no `psiPreGameRun` (o `psiFog` do PS2, 0x121578, é vazio); 4 luzes no Xbox (`light_tag` de 0x40)
  e 2 na VU1 do PS2 (0x50); sombras e luz de personagem e gama por fase só no Xbox (no PS2 `bShadowCharacter` e
  `bCharacterLight` são escritos e nunca lidos); o rastro aplicado no `psiPostDraw` do Xbox e no `CreateSqueezeFrame` do
  `psiPreDraw` do PS2; as partículas em point sprites (`size × 100 × zoom`) contra `Ps2Far1 × 0,002386635`; os
  "loadables" reais no PS2 (`LoadableLoad` 0x1d69e8 carrega `level | (var + 7) << 20` sobre as animações) e vestigiais no
  Xbox; `Mem_Shrink` real no PS2 (0x1cfa50); o `SFXUpdate` do EE (0x1156c0) só age quando o VBlank muda (o "PS2-specific
  stuff" do `game.cpp` deles); a tela de carga com 6 bolinhas a cada 12 quadros no Xbox e 7 × 6 células a cada 11 VBlanks
  no PS2; `cosf`/`sinf` no Xbox, `PS2Sinf3` no PS2; `obj_tag` 0xe4/0x110, `viewer` 0x208/0x240, `DYNAMICSOUNDS`
  0x48/0x60 (iguais: `sprite`, `HUDINFO`, `HUDPANE`, `M_MANAGER`, `weapon_definition_tag`, `GameState`).

### 3.5 A cobertura deles e o GameCube

`docs/function-coverage.md` (7/10/2026): no `default.xbe`, 451 funções do jogo substituídas e 24 mortas de 2.500 (19%,
22% dos bytes); por módulo, a missão 100%, a luz 73%, a entrada 57%, os scripts 55%, o fluxo 53%, o carregador 47%, o
render 34%, o multijogador 26%, o jogador 24%, a interface 23%, os objetos 17%, a colisão 12%, o áudio 11%, a animação
5%, a IA 3%, os bots 8%, a navegação 4%, efeitos, corpos rígidos e saves 0% (o resto roda o x86 original, por isso o jogo
inteiro joga, multijogador incluído). No `Driving.xbe`, a engine 97% e os veículos 100%, o jogo e a engine 61%. Nós: 2.103
funções do `ACTION.ELF` (79% dos bytes), inclusive tudo o que eles têm em 0-5%; eles estão à frente no multijogador, nos
bots e na engine de carro.

`docs/gamecube-checks.md`: a versão de GameCube só lhes serve para recolher as verificações de depuração (mensagens e
erros fatais) que o Xbox perdeu (33 aplicadas, 75 pendentes, 46 só no GameCube); não comparam comportamento. O
`ACTION.ELF` do PS2 também não tem essas mensagens; a lista sugere guardas para o nosso porte (a pele Bondcombat
obrigatória, mais de 0xff nós de cobertura ou AIPoints, mais de 1000 `HITDATA`, o estouro da pilha do GameFlow).

### 3.6 O que nós sabemos e eles não (a pé)

O material 0x10 é GLASS (responde a pergunta deles sobre a bandeira de projétil 0x800: o projétil para no vidro); o bit 0
de `HITTEST`+0x88 é "apoiado no chão" (`player.cpp:1974`, `drone_move.cpp:811`); quem liga os bits de obj+0xd9 (bala 2,
jogador e drones 1, portas 8); os tipos de teste por objeto; no PS2 a neve e as águas têm modelo de detrito mas o byte
+0x82 é 0; a leitura certa de +0x14c, +0x3d, do controle da Zoe e do `DroneModeSettings` (3.2); as particularidades da
navegação do PS2 (`ainetwork.h:26-35`); o quirk do `Menu_UpdateMessageBox` (0x1f7de8: um save sem `ls`+0x3d ou sem nome
trava o cartão); os campos do MPSG (o radar, o handicap de −75 a 100) e do PLRS (+0x0b "HUD sempre visível" invertido,
+0x0c "objetos piscando" = 2); os créditos e os 52 códigos secretos conferidos rodando o código do ELF (162 de 162).
E da arquitetura: o PS2 cai para 30 Hz sob carga (o "passo fixo" é do Xbox); o pacote 07000500 traz as imagens da tela
de carga e os scripts das legendas dos filmes, usado antes do `Mem_Init` o apagar; o sentido dos eventos de música 2-47
(`src/engine/music.cpp`; `MapDroneData`/`MusicVars` são parâmetros de alerta da IA, não de "ducking"); os "loadables" e
o `Mem_Shrink` reais do PS2; cel+0x90 é o nível da água (o `Camera_Shear` é a câmera debaixo d'água, não a morte por
queda); o estado que o `Game_Draw` muda a cada desenho (o giro do céu, os raios, o `GlobalBlur`, os sorteios do `Rand`),
que é por que o nosso porte interpola em vez de trocar a taxa dos ticks.

## 4. Métodos

- **A validação do casamento.** O README do `driving-symbol-matching` deles mede o "infill" e o alinhamento escondendo
  30% das linhas conhecidas e refazendo: 99,8% no "exact", 91% dentro das corridas de tamanhos iguais (o ponto fraco que
  a seção 1 achou também no nosso `driving_sym.py`), 99,5% no alinhamento com margem >= 3. A nossa avaliação
  (`--evaluate`) esconde metade dos trechos de tamanho exato e mede 99-100%, mas não pega um deslocamento sistemático
  dentro de uma corrida; vale acrescentar ao `driving_sym.py` uma margem por linha como a deles e tirar do "tamanho
  exato" as corridas de funções de tamanho igual.
- **O que conferir antes de usar um fato deles**: os endereços do Xbox valem no nosso `out/ghidra_xbox`; os do PS2 valem
  no `DRIVING.ELF` (mesmo leiaute) mas não no `ACTION.ELF` (o deles é o europeu); os leiautes de estrutura precisam das
  regras da seção 2.1; a aritmética é x87.

## 5. Efeito no plano das missões de carro

| Fase | Antes (por fase, aproximado; o total da seção 11 do `docs/driving.md`: 26-39) | Agora | Por quê |
|---|---|---|---|
| 0. Infraestrutura (o resto: os resolvedores do CARP, os formatos do mundo) | 1-2 | 0,5-1 | os 20 resolvedores, `BaseDesc`, `Instance`, `CDat`, `RNgp`, `.atr`/`.tun`/`.def` descritos |
| 1. Carro na pista | 5-8 | 3,5-6 | `CollideWithObject`/`CollideWithWorld`, `WCollisionMgr`, `WGrid`, `WWorldPos`, `PBondCar` e as medidas do Vanquish legíveis e com nome; o render: a `SpecularMat`, as luzes, o brilho por carro, o reflexo e a sombra lidos, e a estrutura do vidro e do switch |
| 2. Mundo vivo e som | 7-11 | 5-8 | a SND (com o EA-XA) como oráculo do algoritmo; glares, sombra, decals, céu, água, neblina, partículas; as estradas e os atores |
| 3. Missões completas | 9-14 | 8-13 | eles não portaram missões, eventos, IA, armas nem HUD: só nomes (os eventos `E*` com par >= 2 em 69% dos bytes) e estruturas (`SMissionManager`) |
| 4. Integração e PC | 3-5 | 2,5-4 | a página de troca confirmada e nomeada, o fluxo de relançamento entre as partes, o `Scheduler` |
| **total** | **26-39** | **20-32** | |

A economia está em ler menos, não em portar menos: o código deles é a versão MSVC/x87 do Xbox, então cada função ainda
é portada do MIPS e conferida no interpretador (seção 11 do `docs/driving.md`). A incerteza continua alta; o que mais pode
estourar ainda é o render do mundo (os métodos da pista, que nem eles têm por método) e as missões (o nível do jogo, que
ninguém portou).

## 6. O que eles ganham de nós

Para o usuário levar na conversa com eles (fatos, curtos):
- **A física do carro conferida bit a bit contra o original**: o corpo rígido do `DRIVING.ELF` portado, o microcódigo da
  VU0 reescrito à mão, e o `nf_driving_check`, que roda o código original do EE e da VU0 em interpretadores e compara com
  o porte (35 milhões de instruções MIPS, 10.690 microprogramas, 0 diferenças); um interpretador de VU0/VU1 com as
  latências e a aritmética do PS2.
- **O casamento Xbox ↔ PS2 por características** (`tools/xbox_match.py`: cadeias, constantes float, globais, vizinhos,
  tabelas virtuais; 3.629 pares agora) e o **casamento do `DRIVING.SYM`** com o ELF de varejo por âncoras estruturais
  (type_info, construtores pelas tabelas virtuais, bibliotecas pelo padrão de bytes; `tools/driving_sym.py`), que não
  depende da planilha.
- **O render do carro no PS2**: a família `MatCarTexturePacked`/`MatCarChrome`/`MatCarTextureDullPacked`/`MatCarDash`
  lida no microprograma da VU1 e conferida contra ele (494 mil vértices), o carregador do ELF do EAGL, o `GeoPrim::Draw`,
  o VIF, o censo dos 115 métodos.
- **Os formatos decodificados do lado do PS2**: SHPS (com CSM1 e o PSMT8 embaralhado), o BIGF e o RefPack, o CARP como
  UGroup, os bancos e fluxos da EA do PS2, os 12 `.mpc`.
- **A troca do lado do `ACTION`**: o bloco UberBond inteiro do lado a pé (o codinome, as opções, a pontuação, as tabelas
  das missões de carro), o `BASE.ELF` e o fluxo de volta com os resultados 0-4.
- **A engine a pé portada quase inteira** (2.103 funções, 79% dos bytes do `ACTION.ELF`), com o driver de som `SFX.IRX`
  portado (152 funções) e a SPU2 em software, o cartão de memória e o perfil, os 52 códigos secretos e os créditos
  conferidos rodando o código do ELF num interpretador MIPS; a IA a pé (570 das 590 funções, a rede de navegação
  inteira) com uns 250 campos do `Drone_tag` nomeados.
- **Erros que este estudo achou nos docs e no código deles** (seção 3.2) e o que sabemos e eles não (seção 3.6).

## 7. O que não foi conferido

- Nada do lado do Xbox foi aberto no Ghidra para conferir as afirmações deles que só valem para o Xbox (funções fundidas,
  as constantes do `Driving.xbe`, o `Corona_Create` do Xbox, o padrão do `Car_Create` no XBE): vêm dos docs e do código
  deles.
- A igualdade de leiaute do `DRIVING.ELF` europeu com o nosso foi medida pelos endereços e nomes (seção 1), não
  comparando os binários (não temos o europeu).
- `ReadTuningVars`: os stores sem condição foram lidos na desmontagem; os oito blocos por fase não foram decodificados
  um a um (os valores vêm do código deles, que bate com os stores do PS2 na leitura deste estudo). Conferidos depois, no
  porte (12/10/2026): cada bloco simulado na desmontagem e o código do ELF rodado no interpretador MIPS contra o porte
  para 83 códigos de fase, sem diferença.
- No render do carro: `RCARPFile::RegisterSymbols` do PS2 (0x1a8930: o `SwitchData` sem índice, o `ReflectionMap`), o
  sinal das direções das luzes e o K das luzes pontuais no PS2 (0x1aedc8), o `RReflection::Draw` inteiro, o leiaute do
  `FogData` contra a VU 0x48, o `RWater::Update` do PS2, quem grava `SwitchData`/`DamageZones` no PS2.
- Na troca: os campos +0x5d8, +0x5dc, +0x614, +0x618, +0x624 do lado do `DRIVING` (só o +0xa14 foi conferido) e a
  "trapaça" que levaria à missão 8.
- O efeito em jogo das lacunas da seção 3.1 (deduzido pelo caminho do código; nada foi executado) e se algum script do
  disco grava um canal com valor par (o que tornaria visível o desvio do `Mission_MonitorObjectives`).
- Os nomes importados por pares de confiança 1 e os das bibliotecas da EA (seção 1, "Cuidado").
