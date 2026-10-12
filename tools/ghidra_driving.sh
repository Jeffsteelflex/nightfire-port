#!/usr/bin/env bash
# O DRIVING.ELF (as missões de carro) no Ghidra: um projeto headless próprio (out/ghidra_driving, projeto "Driving"),
# separado dos projetos do ACTION.ELF (out/ghidra_headless, ghidra/) e dos IRX (out/ghidra_irx).
#
#   tools/ghidra_driving.sh import                     importa e analisa o DRIVING.ELF (r5900, uma vez, alguns minutos)
#   tools/ghidra_driving.sh import-base                importa o BASE.ELF (o carregador que troca os dois ELFs)
#   tools/ghidra_driving.sh base <script> [args]       roda um pós-script no BASE.ELF (ExportDecompiled, ExportDisasm...)
#   tools/ghidra_driving.sh names <nomes.tsv>          aplica os nomes de tools/driving_sym.py ghidra (endereço, tipo, nome)
#   tools/ghidra_driving.sh functions                  exporta as funções e as chamadas (out/driving/functions.tsv, calls.tsv)
#   tools/ghidra_driving.sh features                   o que o casador com o Xbox usa (out/driving/features.tsv; xbox_match.py)
#   tools/ghidra_driving.sh decomp <nome|re:regex>...  decompila para out/decomp_driving/
#   tools/ghidra_driving.sh disasm <nome>...           desmontagem para out/disasm_driving/
#
# O DRIVING.ELF não tem .symtab; os nomes vêm do DRIVING/DRIVING.SYM, que é de outra compilação (ver docs/driving.md):
# tools/driving_sym.py casa as funções das duas e só exporta os nomes casados. A raiz dos dados é o checkout com os
# arquivos do disco (NF_DATA, padrão: a raiz do repositório); o projeto e as saídas ficam no out/ dela (o Ghidra recusa
# caminhos com um elemento começando por '.', como .claude/worktrees).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA="${NF_DATA:-$ROOT}"
GHIDRA="${GHIDRA_HOME:-/c/Tools/ghidra_12.1.3_PUBLIC}"
HEADLESS="$GHIDRA/support/analyzeHeadless.bat"
OUT="$DATA/out"
PROJ_DIR="$(cygpath -w "$OUT/ghidra_driving" 2>/dev/null || echo "$OUT/ghidra_driving")"
SCRIPTS="$(cygpath -w "$ROOT/tools/ghidra_scripts" 2>/dev/null || echo "$ROOT/tools/ghidra_scripts")"
w() { cygpath -w "$1" 2>/dev/null || echo "$1"; }
mkdir -p "$OUT/ghidra_driving" "$OUT/driving"

case "${1:-}" in
  import)
    "$HEADLESS" "$PROJ_DIR" Driving -import "$(w "$DATA/DRIVING.ELF")" -overwrite \
      -processor "r5900:LE:32:default" \
      -scriptPath "$SCRIPTS" -preScript ConfigureAnalysis.java
    ;;
  import-base)
    "$HEADLESS" "$PROJ_DIR" Driving -import "$(w "$DATA/BASE.ELF")" -overwrite \
      -processor "r5900:LE:32:default" \
      -scriptPath "$SCRIPTS" -preScript ConfigureAnalysis.java
    ;;
  base)
    [ -n "${2:-}" ] || { sed -n '2,15p' "$0"; exit 1; }
    script="$2"; shift 2
    "$HEADLESS" "$PROJ_DIR" Driving -process BASE.ELF -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript "$script" "$@"
    ;;
  names)
    [ -n "${2:-}" ] || { sed -n '2,15p' "$0"; exit 1; }
    "$HEADLESS" "$PROJ_DIR" Driving -process DRIVING.ELF \
      -scriptPath "$SCRIPTS" -postScript ApplySymbolTable.java "$(w "$2")"
    ;;
  functions)
    "$HEADLESS" "$PROJ_DIR" Driving -process DRIVING.ELF -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript ExportAllCalls.java \
      "$(w "$OUT/driving/functions.tsv")" "$(w "$OUT/driving/calls.tsv")"
    ;;
  features)
    "$HEADLESS" "$PROJ_DIR" Driving -process DRIVING.ELF -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript ExportMatchFeatures.java "$(w "$OUT/driving/features.tsv")"
    ;;
  decomp)
    shift
    mkdir -p "$OUT/decomp_driving"
    "$HEADLESS" "$PROJ_DIR" Driving -process DRIVING.ELF -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript ExportDecompiled.java "$(w "$OUT/decomp_driving")" "$@"
    ;;
  disasm)
    shift
    mkdir -p "$OUT/disasm_driving"
    "$HEADLESS" "$PROJ_DIR" Driving -process DRIVING.ELF -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript ExportDisasm.java "$(w "$OUT/disasm_driving")" "$@"
    ;;
  *)
    sed -n '2,15p' "$0"; exit 1 ;;
esac
