# Ambiente de desenvolvimento

## Compilador

O build usa o **Visual Studio Build Tools 2026** (MSVC 14.51, CMake 4.3, Ninja),
instalado em `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`.

O port é um programa de **32 bits** (x86): as estruturas do jogo guardam ponteiros de 4 bytes, e
assim o layout delas fica igual ao do PS2. O `build.bat` já carrega o ambiente certo; para usar num
terminal, carregue o ambiente x86 antes:

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
cmake -S . -B build -G Ninja
cmake --build build
```

## Ferramentas de análise

- Python 3.12 para os scripts em `tools/`.
- Ghidra 12.1.3 em `C:\Tools\ghidra_12.1.3_PUBLIC` (abrir com `ghidraRun.bat`).
  Requer JDK 21 ou mais novo; a máquina usa o JDK 23.
- Plugin ghidra-emotionengine-reloaded v2.1.37, build para Ghidra 12.1.3, em
  `C:\Tools\downloads`. Instalar pelo Ghidra em File > Install Extensions.
  O plugin precisa da versão exata do Ghidra: não atualizar o Ghidra sem trocar o plugin.
- Projeto do Ghidra em `ghidra/` na raiz do repositório (ignorado pelo git).
  Na análise, ligar **GNU Demangler > Use Deprecated Demangler** (nomes do GCC 2.95).
- PCSX2 com debugger (pendente).

## Extração dos dados

```
python tools/filesbin.py extract           # FILES.BIN -> extracted/files/
python tools/levelpak.py extract extracted/files/07000001.bin
```

## Ghidra em modo headless

O projeto da interface gráfica fica travado enquanto aberto. Para scripts,
há um segundo projeto em `out/ghidra_headless/`:

```
tools/ghidra_headless.sh import                      # importa e analisa o ACTION.ELF (cerca de 1 minuto)
tools/ghidra_headless.sh decomp LoaderLoad "re:Anim.*"  # exporta C decompilado para out/decomp/
tools/ghidra_headless.sh callers SkinIt FRAME_RATE      # quem chama uma função ou usa uma variável
tools/ghidra_headless.sh callgraph 2 main               # grafo de chamadas a partir de uma função
```

Desmontagem (para ver as máscaras de destino da VU0, como `vmul.xyz`, que o decompilador omite).
Um padrão por argumento, porque o `cmd` interpreta `|`:

```
GH=/c/Tools/ghidra_12.1.3_PUBLIC/support/analyzeHeadless.bat
"$GH" "$(cygpath -w out/ghidra_headless)" Nightfire -process ACTION.ELF -noanalysis -readOnly \
  -scriptPath "$(cygpath -w tools/ghidra_scripts)" \
  -postScript ExportDisasm.java "$(cygpath -w out/disasm)" "re:^Vec_.*" "Mat_LookAt"
```

## O DRIVING.ELF no Ghidra

Um terceiro projeto headless, `out/ghidra_driving` (separado dos do `ACTION.ELF` e dos IRX), com o `DRIVING.ELF` e o
`BASE.ELF`. O `DRIVING.ELF` não tem `.symtab`: os nomes vêm do `DRIVING/DRIVING.SYM`, de outra compilação, casados por
`tools/driving_sym.py` (`docs/driving.md`, "O DRIVING.SYM"). Num worktree, `NF_DATA` aponta o checkout com os dados:

```
tools/ghidra_driving.sh import                               # importa e analisa (cerca de 1 minuto)
tools/ghidra_driving.sh functions                            # out/driving/functions.tsv e calls.tsv
python tools/driving_sym.py match --evaluate                 # out/driving/sym_map.tsv, com a precisão
python tools/driving_sym.py ghidra out/driving/sym_map.tsv out/driving/ghidra_names.tsv
tools/ghidra_driving.sh names out/driving/ghidra_names.tsv   # aplica os nomes
tools/ghidra_driving.sh decomp main "re:SMissionManager::.*" # out/decomp_driving/
tools/ghidra_driving.sh import-base                          # o BASE.ELF no mesmo projeto
tools/ghidra_driving.sh base ExportDecompiled.java <pasta> FUN_00a000e0
```

Os dados da pasta `DRIVING/` (BIGF, RefPack): `python tools/driving_data.py stats|list|extract|ssh|carp|asf|eagl`.

## Microprogramas da VU (`tools/vudis.py`)

O desmontador lê as cadeias de DMA da seção `.vutext` do `ACTION.ELF` (comandos `MPG` da VIF), monta a
memória de microprograma da VU1 (a primeira cadeia) ou da VU0 (a segunda, `--vu0`) e põe os rótulos da
tabela de símbolos (`_$ROTATE_LIGHT`...). Cada linha mostra o endereço em instruções e em bytes, os bits
de controle da palavra de cima (`I` = a de baixo é um `LOI`, `E` = fim depois do par seguinte, `M`,
`D`, `T`), a instrução de cima e a de baixo; os desvios mostram o destino e o rótulo.

```
python tools/vudis.py ACTION.ELF 0x340 0x3d6       # [início, fim) em instruções (x8 = bytes)
python tools/vudis.py ACTION.ELF ROTATE_LIGHT      # pelo rótulo, até o par depois do bit E ou do JR
python tools/vudis.py ACTION.ELF                   # a VU1 inteira
python tools/vudis.py ACTION.ELF --vu0             # o skinning da VU0
python tools/vudis.py ACTION.ELF --labels          # só os rótulos
python tools/vudis.py --test                       # confere a decodificação com palavras conhecidas
```

O VCL (que gerou o código) intercala as iterações dos laços: para seguir um vértice, leia o prólogo
antes do rótulo `*_LOOP` e lembre que `DIV` só escreve Q 7 ciclos depois (`RSQRT`, 13) e `ERSADD`
escreve P 18 ciclos depois (cada instrução da EFU tem a sua latência): um `MULq` antes disso ainda lê o
Q anterior. As rotinas que importam
estão em `docs/formats.md`, "Microprograma da VU1".

## Decompilação

O código portado do ACTION.ELF fica em `src/engine/`, com o marcador
`// [decomp] 0x<endereço> <Nome>` acima de cada função. O fluxo completo está no `CLAUDE.md`.

```
python tools/decomp_status.py               # progresso por módulo; falha se um marcador estiver errado
python tools/decomp_status.py --missing Vec_ # o que falta em um módulo
./build/Release/nf_tests.exe                 # testes de unidade
./build/Release/nf_level_check.exe .         # regressão com os dados: monta as 35 fases
```

Para o mapa de camadas (`docs/engine.md`), exporte o grafo completo e o uso de hardware e rode
`tools/layers.py`:

```
GH=/c/Tools/ghidra_12.1.3_PUBLIC/support/analyzeHeadless.bat
"$GH" "$(cygpath -w out/ghidra_headless)" Nightfire -process ACTION.ELF -noanalysis -readOnly   -scriptPath "$(cygpath -w tools/ghidra_scripts)"   -postScript ExportAllCalls.java "$(cygpath -w out/functions.tsv)" "$(cygpath -w out/calls.tsv)"   -postScript ExportHwUse.java "$(cygpath -w out/hwuse.tsv)"
python tools/layers.py > out/layers_report.txt
```

## Visualizador de fases

```
python tools/geometry.py level extracted/files/07000001.bin   # fase montada em extracted/models/07000001.glb
python -m http.server 8137 --bind 127.0.0.1                    # na raiz do repositório
```

Para escolher a fase pelo nome, gere o catálogo antes:

```
python tools/levels.py --json extracted/models/levels.json
```

Abra `http://localhost:8137/tools/viewer/index.html` e escolha a fase na lista.
Parâmetros: `level` (hash da fase a abrir), `models` (lista de `.glb`, ignora o catálogo),
`view` (`iso`, `top`, `front`, `back`, `left`, `right`) e `zoom` (multiplicador da
distância). Os `.glb` também abrem no Blender.

### Galeria de personagens

```
python tools/geometry.py characters extracted/files/070*.bin   # extracted/models/characters/*.glb
```

Abra `http://localhost:8137/tools/viewer/index.html?gallery=/extracted/models/characters/`.
Parâmetros: `offset`, `limit` (padrão 40), `cols` (padrão 10) e `spacing`.
Também valem `bones=1` (mostra o esqueleto) e `bend=20:-70,32:70`, que gira os ossos
indicados em torno de Z, em graus, para testar o skinning.

### Animações

```
python tools/anim.py 07000001          # bibliotecas da fase em extracted/models/anims/
```

Abra o `.glb` gerado com `?models=/extracted/models/anims/<arquivo>.glb` e escolha a
animação na lista. Parâmetros: `anim` (nome ou índice), `t` (congela no segundo indicado),
`speed` (multiplicador de velocidade) e `fps` (para o contador de quadros, padrão 30).

### Rostos (alvos de morph)

```
python tools/morph.py list extracted/files/070*.bin                       # as cabeças (bloco 0x2B) e as peles com morphs
python tools/morph.py targets extracted/files/07000001.bin 01000145      # os 22 alvos da cabeça do Mayhew
python tools/morph.py faceseq extracted/files/07000001.bin extracted/files/07F00001.bin 060000f3   # a piscada, quadro a quadro
python tools/morph.py gltf extracted/files/07000001.bin 01000145 extracted/models/mayhew.glb --face 0600055d
```

O `.glb` traz os 22 alvos como morph targets glTF e, com `--face`, uma animação dos pesos (a sequência de rosto do
script, procurada também nos pacotes de animação da fase). No visualizador:
`?models=/extracted/models/mayhew.glb&anim=0&fps=60&t=10.55&view=front&zoom=0.1&focus=0,0.58,0.14` (`focus` põe o
centro da câmera num ponto; a boca do Mayhew abre em 10,55 s, o "Ah, Mr. Bond" da cena de abertura).
