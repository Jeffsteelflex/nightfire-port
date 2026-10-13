"""Confere a separação entre o código neutro e os backends de plataforma (docs/port.md, "Plataforma: interfaces e
backends").

Uso: python tools/check_platform.py [--quiet]
Lê as regras de tools/platform_rules.txt e procura em src/ e tests/ (os .cpp, .h, .inc e .mm) cada cabeçalho, identificador e
#pragma proibido fora dos arquivos de backend e das exceções da regra. Os comentários e os textos entre aspas não contam.
Sai com 1 (e a lista, arquivo:linha) se achar algum; 0 sem nenhum. O nf_tests faz a mesma conferência
(tests/test_platform.cpp), com as mesmas regras.
"""
import fnmatch
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RULES = os.path.join(ROOT, "tools", "platform_rules.txt")
SCAN = ("src", "tests")
EXTS = (".cpp", ".h", ".inc", ".mm")


def load_rules(path=RULES):
    backends, rules = [], []
    section = None
    for raw in open(path, encoding="utf-8"):
        line = raw.split("#", 1)[0].strip() if not raw.lstrip().startswith("#") else ""
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        if section == "backends":
            backends.append(line)
        elif section == "regras":
            parts = line.split()
            kind, name, allowed = parts[0], parts[1], parts[2:]
            rules.append((kind, name, allowed))
    return backends, rules


def strip_code(text):
    """Os comentários e os textos entre aspas viram espaços (as quebras de linha ficam: as linhas não mudam).

    Os nomes de #include "..." ficam (são o que a regra include procura)."""
    out = []
    i, n = 0, len(text)
    line_start = True
    include_line = False
    while i < n:
        c = text[i]
        if line_start:
            m = re.match(r"[ \t]*#[ \t]*include\b", text[i:i + 64])
            include_line = m is not None
        line_start = c == "\n"
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
            continue
        if c == "R" and i + 1 < n and text[i + 1] == '"' and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            # literal bruto R"delim( ... )delim"
            k = text.find("(", i + 2)
            delim = text[i + 2:k]
            j = text.find(")" + delim + '"', k)
            j = n if j < 0 else j + len(delim) + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
            continue
        if c in "\"'" and not (c == '"' and include_line):
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + " " * (j - i - 2) + (c if j - i >= 2 else ""))
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)


def matches(pattern, value, nocase=False):
    if nocase:
        pattern, value = pattern.lower(), value.lower()
    if pattern.endswith("*"):
        return value.startswith(pattern[:-1])
    return value == pattern


def path_allowed(rel, globs):
    return any(fnmatch.fnmatchcase(rel, g) for g in globs)


def check_file(rel, text, backends, rules):
    if path_allowed(rel, backends):
        return []
    code = strip_code(text)
    found = []
    for no, line in enumerate(code.split("\n"), 1):
        m = re.match(r"\s*#\s*include\s*[<\"]([^>\"]+)[>\"]", line)
        if m:
            header = m.group(1).replace("\\", "/")
            for kind, name, allowed in rules:
                if kind == "include" and matches(name, header, nocase=True) and not path_allowed(rel, allowed):
                    found.append((rel, no, f"include <{header}>"))
            continue
        m = re.match(r"\s*#\s*pragma\s+(\w+)", line)
        if m:
            for kind, name, allowed in rules:
                if kind == "pragma" and m.group(1) == name and not path_allowed(rel, allowed):
                    found.append((rel, no, f"#pragma {name}"))
        for tok in set(re.findall(r"[A-Za-z_]\w*", line)):
            for kind, name, allowed in rules:
                if kind == "token" and matches(name, tok) and not path_allowed(rel, allowed):
                    found.append((rel, no, tok))
    return found


def main():
    backends, rules = load_rules()
    problems = []
    files = 0
    for top in SCAN:
        for dirpath, _, names in os.walk(os.path.join(ROOT, top)):
            for name in sorted(names):
                if not name.endswith(EXTS):
                    continue
                path = os.path.join(dirpath, name)
                rel = os.path.relpath(path, ROOT).replace("\\", "/")
                files += 1
                text = open(path, encoding="utf-8", errors="replace").read()
                problems += check_file(rel, text, backends, rules)
    for rel, no, what in sorted(problems):
        print(f"{rel}:{no}: {what}")
    if "--quiet" not in sys.argv or problems:
        print(f"{files} arquivos, {len(rules)} regras, {len(backends)} padrões de backend: "
              f"{len(problems)} usos do sistema fora dos backends")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
