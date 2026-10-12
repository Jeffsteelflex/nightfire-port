#!/usr/bin/env bash
# Módulos do IOP (MODULES/*.IRX) no Ghidra: um projeto headless próprio (out/ghidra_irx), separado do ACTION.ELF.
#
#   tools/ghidra_irx.sh import <MÓDULO.IRX>               importa e analisa (MIPS R3000, 32 bits little-endian)
#   tools/ghidra_irx.sh decomp <MÓDULO.IRX> <nome|re:regex>...   decompila para out/decomp_irx/<MÓDULO>/
#   tools/ghidra_irx.sh disasm <MÓDULO.IRX> <nome>...     desmontagem para out/disasm_irx/<MÓDULO>/
#
# Os IRX são ELF relocáveis (e_type 0xff80) com .symtab e STABS (.mdebug): o Ghidra os carrega com a linguagem
# MIPS:LE:32:default e aplica as relocações; os nomes das funções vêm da .symtab. O endereço base escolhido pelo
# Ghidra pode diferir do da tabela: tools/sound.py irxsyms lista os endereços relativos ao começo do .text.
# A raiz dos dados é o checkout com os arquivos do disco (NF_DATA, padrão: a raiz do repositório); o projeto e as
# saídas ficam no out/ dela (o Ghidra recusa caminhos com um elemento começando por '.', como .claude/worktrees).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA="${NF_DATA:-$ROOT}"
GHIDRA="${GHIDRA_HOME:-/c/Tools/ghidra_12.1.3_PUBLIC}"
HEADLESS="$GHIDRA/support/analyzeHeadless.bat"
OUT="$DATA/out"
PROJ_DIR="$(cygpath -w "$OUT/ghidra_irx" 2>/dev/null || echo "$OUT/ghidra_irx")"
SCRIPTS="$(cygpath -w "$ROOT/tools/ghidra_scripts" 2>/dev/null || echo "$ROOT/tools/ghidra_scripts")"
mkdir -p "$OUT/ghidra_irx"

cmd="${1:-}"; mod="${2:-}"
[ -n "$cmd" ] && [ -n "$mod" ] || { sed -n '2,10p' "$0"; exit 1; }
shift 2
base="${mod%.*}"
case "$cmd" in
  import)
    "$HEADLESS" "$PROJ_DIR" IOP -import "$(cygpath -w "$DATA/MODULES/$mod")" -overwrite \
      -processor "MIPS:LE:32:default"
    ;;
  decomp)
    mkdir -p "$OUT/decomp_irx/$base"
    "$HEADLESS" "$PROJ_DIR" IOP -process "$mod" -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript ExportDecompiled.java "$(cygpath -w "$OUT/decomp_irx/$base")" "$@"
    ;;
  disasm)
    mkdir -p "$OUT/disasm_irx/$base"
    "$HEADLESS" "$PROJ_DIR" IOP -process "$mod" -noanalysis -readOnly \
      -scriptPath "$SCRIPTS" -postScript ExportDisasm.java "$(cygpath -w "$OUT/disasm_irx/$base")" "$@"
    ;;
  *)
    sed -n '2,10p' "$0"; exit 1 ;;
esac
