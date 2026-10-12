#!/usr/bin/env bash
# Projeto Ghidra headless separado do projeto da interface gráfica (que fica travado enquanto aberto).
#
#   tools/ghidra_headless.sh import                     importa e analisa o ACTION.ELF (uma vez, alguns minutos)
#   tools/ghidra_headless.sh decomp <nome|re:regex>...  exporta funções decompiladas para out/decomp/
#   tools/ghidra_headless.sh callers <nome>...          lista quem chama cada função (out/callers.txt)
#   tools/ghidra_headless.sh callgraph <prof> <nome>... grafo de chamadas (out/callgraph.txt)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GHIDRA="${GHIDRA_HOME:-/c/Tools/ghidra_12.1.3_PUBLIC}"
HEADLESS="$GHIDRA/support/analyzeHeadless.bat"
PROJ_DIR="$(cygpath -w "$ROOT/out/ghidra_headless" 2>/dev/null || echo "$ROOT/out/ghidra_headless")"
SCRIPTS="$(cygpath -w "$ROOT/tools/ghidra_scripts" 2>/dev/null || echo "$ROOT/tools/ghidra_scripts")"
mkdir -p "$ROOT/out/ghidra_headless" "$ROOT/out/decomp"

case "${1:-}" in
  import)
    "$HEADLESS" "$PROJ_DIR" Nightfire -import "$(cygpath -w "$ROOT/ACTION.ELF")" -overwrite \
      -processor "r5900:LE:32:default" \
      -scriptPath "$SCRIPTS" -preScript ConfigureAnalysis.java
    ;;
  decomp)
    shift
    "$HEADLESS" "$PROJ_DIR" Nightfire -process ACTION.ELF -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript ExportDecompiled.java "$(cygpath -w "$ROOT/out/decomp")" "$@"
    ;;
  callers)
    shift
    "$HEADLESS" "$PROJ_DIR" Nightfire -process ACTION.ELF -noanalysis -readOnly       -scriptPath "$SCRIPTS" -postScript ListCallers.java "$(cygpath -w "$ROOT/out/callers.txt")" "$@"
    cat "$ROOT/out/callers.txt"
    ;;
  callgraph)
    shift
    "$HEADLESS" "$PROJ_DIR" Nightfire -process ACTION.ELF -noanalysis -readOnly       -scriptPath "$SCRIPTS" -postScript ExportCallGraph.java "$(cygpath -w "$ROOT/out/callgraph.txt")" "$@" >/dev/null
    cat "$ROOT/out/callgraph.txt"
    ;;
  *)
    sed -n '2,8p' "$0"; exit 1 ;;
esac
