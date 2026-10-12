#!/usr/bin/env bash
# A versão de Xbox no Ghidra, só como referência de leitura (docs/driving.md, "A versão de Xbox"): um projeto headless
# próprio (out/ghidra_xbox, projeto "Xbox"), separado dos do PS2. O XBE vira um PE32 equivalente
# (tools/xbox.py pe) para o carregador PE e os analisadores do MSVC (x86, especificação "windows").
#
#   tools/ghidra_xbox.sh pe                              os PE e os rótulos: out/xbox/Driving.exe, default.exe (+ .labels.tsv)
#   tools/ghidra_xbox.sh import [Driving|default]        importa e analisa (Driving.exe por padrão; 1-2 minutos) e cria
#                                                        as funções das tabelas virtuais (XboxVtables.java)
#   tools/ghidra_xbox.sh names <nomes.tsv> [programa]    aplica uma tabela de nomes (ApplySymbolTable.java)
#   tools/ghidra_xbox.sh functions [programa]            funções e chamadas (out/xbox/<programa>_functions.tsv, _calls.tsv)
#   tools/ghidra_xbox.sh features [programa]             o que o casador usa (out/xbox/<programa>_features.tsv)
#   tools/ghidra_xbox.sh decomp <nome|re:regex>...       decompila o Driving.exe para out/xbox/decomp/
#   tools/ghidra_xbox.sh disasm <nome>...                desmontagem do Driving.exe para out/xbox/disasm/
#   tools/ghidra_xbox.sh callers <nome>...               quem chama ou referencia (funções e globais: DAT_00234e30)
#
# A cópia do disco fica em xbox/ (no .gitignore): xbox/disco/Driving.xbe e default.xbe (tools/xbox.py extract). A raiz
# dos dados é o checkout com os arquivos (NF_DATA, padrão: a raiz do repositório); tudo o que sai fica no out/ dela.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA="${NF_DATA:-$ROOT}"
GHIDRA="${GHIDRA_HOME:-/c/Tools/ghidra_12.1.3_PUBLIC}"
HEADLESS="$GHIDRA/support/analyzeHeadless.bat"
OUT="$DATA/out"
w() { cygpath -w "$1" 2>/dev/null || echo "$1"; }
PROJ_DIR="$(w "$OUT/ghidra_xbox")"
SCRIPTS="$(w "$ROOT/tools/ghidra_scripts")"
mkdir -p "$OUT/ghidra_xbox" "$OUT/xbox"
prog() { echo "${1:-Driving}.exe"; }

case "${1:-}" in
  pe)
    for x in Driving default; do
      python "$ROOT/tools/xbox.py" pe "$DATA/xbox/disco/$x.xbe" "$OUT/xbox/$x.exe"
    done
    ;;
  import)
    p="$(prog "${2:-}")"
    [ -f "$OUT/xbox/$p" ] || { echo "falta $OUT/xbox/$p: rode antes 'tools/ghidra_xbox.sh pe'"; exit 1; }
    "$HEADLESS" "$PROJ_DIR" Xbox -import "$(w "$OUT/xbox/$p")" -overwrite \
      -processor "x86:LE:32:default" -cspec windows \
      -scriptPath "$SCRIPTS" -preScript ApplySymbolTable.java "$(w "$OUT/xbox/${p%.exe}.labels.tsv")" \
      -postScript XboxVtables.java
    ;;
  names)
    [ -n "${2:-}" ] || { sed -n '2,17p' "$0"; exit 1; }
    "$HEADLESS" "$PROJ_DIR" Xbox -process "$(prog "${3:-}")" -noanalysis \
      -scriptPath "$SCRIPTS" -postScript ApplySymbolTable.java "$(w "$2")"
    ;;
  functions)
    p="$(prog "${2:-}")"
    "$HEADLESS" "$PROJ_DIR" Xbox -process "$p" -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript ExportAllCalls.java \
      "$(w "$OUT/xbox/${p%.exe}_functions.tsv")" "$(w "$OUT/xbox/${p%.exe}_calls.tsv")"
    ;;
  features)
    p="$(prog "${2:-}")"
    "$HEADLESS" "$PROJ_DIR" Xbox -process "$p" -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript ExportMatchFeatures.java "$(w "$OUT/xbox/${p%.exe}_features.tsv")"
    ;;
  decomp)
    shift
    mkdir -p "$OUT/xbox/decomp"
    "$HEADLESS" "$PROJ_DIR" Xbox -process Driving.exe -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript ExportDecompiled.java "$(w "$OUT/xbox/decomp")" "$@"
    ;;
  callers)
    shift
    "$HEADLESS" "$PROJ_DIR" Xbox -process Driving.exe -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript ListCallers.java "$(w "$OUT/xbox/callers.txt")" "$@"
    cat "$OUT/xbox/callers.txt"
    ;;
  disasm)
    shift
    mkdir -p "$OUT/xbox/disasm"
    "$HEADLESS" "$PROJ_DIR" Xbox -process Driving.exe -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript ExportDisasm.java "$(w "$OUT/xbox/disasm")" "$@"
    ;;
  *)
    sed -n '2,17p' "$0"; exit 1 ;;
esac
