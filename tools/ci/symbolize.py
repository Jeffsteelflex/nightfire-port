"""Traduz a pilha de uma falha da build headless do macOS ou do Linux (crash_report_posix.cpp) em funções, arquivos e
linhas, na CI (.github/workflows/portable.yml; docs/port.md, "macOS: compilação e CI").

Uso: python tools/ci/symbolize.py <saída do programa> <executável>

O relatório diz onde o executável foi carregado ("imagem <programa> base 0x...") e o backtrace_symbols_fd escreve cada
quadro com o endereço absoluto: no Linux "programa(...)[0x...]", no macOS "N programa 0x... símbolo + d". No Linux o
endereço menos a base vai para o addr2line (o executável é um PIE); no macOS o endereço vai para o atos -l <base>. Só a
biblioteca padrão do Python.
"""
import os
import re
import subprocess
import sys


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    exe = sys.argv[2]
    name = re.escape(os.path.basename(exe))
    # O caminho pode ter espaços (o "Nightfire PC.app/Contents/MacOS/Nightfire PC" do macOS).
    bases = re.findall(r"imagem .*?%s base (0x[0-9a-fA-F]+)" % name, text)
    if not bases:
        print("symbolize: nenhuma pilha do executável na saída")
        return 0
    base = int(bases[-1], 16)
    if sys.platform == "darwin":
        addrs = re.findall(r"^\d+\s+%s\s+(0x[0-9a-fA-F]+)" % name, text, re.M)
        cmd = ["atos", "-o", exe, "-l", hex(base)] + addrs
    else:
        addrs = re.findall(r"%s\([^)]*\)\s*\[(0x[0-9a-fA-F]+)\]" % name, text)
        cmd = ["addr2line", "-C", "-f", "-i", "-p", "-e", exe] + [hex(int(a, 16) - base) for a in addrs]
    if not addrs:
        print("symbolize: nenhum quadro do executável na pilha")
        return 0
    print("symbolize:", " ".join(cmd[:7]), "...")
    out = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(out.stdout)
    sys.stdout.write(out.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
