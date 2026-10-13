"""O zip do macOS para os jogadores (docs/port.md, "macOS: o pacote e a instalação"). Roda num Mac (a CI: o passo do
pacote em .github/workflows/portable.yml), sobre o "Nightfire PC.app" que o CMake montou.

Monta uma pasta "Nightfire PC" com:
  Nightfire PC.app          o pacote universal (arm64 e x86_64), com o ícone e o hd_ui em Contents/Resources (a lista do
                            zip do Windows, make_release.tracked_hd_ui: o do pacote da build é trocado por ela)
  README.txt                o guia de instalação para o Mac (tools/release/README-mac.txt, em inglês)
  LICENSES.txt              o que é de terceiros (tools/release/LICENSES-mac.txt)
e o zip dela (ditto -c -k --keepParent, que guarda o pacote como o Finder espera).

No pacote, antes de assinar: o executável perde a tabela de depuração (strip -S: os caminhos dos objetos da compilação,
com a pasta do usuário, saem; a tabela de símbolos fica, e o relatório de falhas continua nomeando as funções), e o dSYM
(dsymutil, para o atos traduzir os endereços de um relatório) vai para --dsym, fora do zip. Depois a assinatura ad hoc
(codesign --force --deep -s -: sem a conta paga da Apple; o arm64 não roda nada sem ao menos esta) e a conferência dela.
Antes do zip, a mesma regra do zip do Windows (tools/make_release.py, check_tree): nada do disco do jogo, nenhum arquivo
grande além do executável, e nem o nome e o e-mail da conta do git nem a pasta do usuário do Mac em nenhum arquivo. Nada
é publicado: na CI o zip sobe como artefato.

Uso (num Mac):
  python3 tools/make_release_mac.py --app "build/Nightfire PC.app" [--version v0.4] [--out <pasta>] [--dsym <pasta>]
                                    [--allow-thin]
"""

import argparse
import os
import plistlib
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_release  # noqa: E402  (a regra do que pode entrar e a busca dos nomes)

ROOT = make_release.ROOT


def run(*cmd, capture=False):
    r = subprocess.run(list(cmd), capture_output=True, text=True)
    if r.returncode != 0:
        sys.stdout.write(r.stdout)
        sys.stdout.write(r.stderr)
        sys.exit("falhou: " + " ".join(cmd))
    if not capture:
        sys.stdout.write(r.stdout)
        sys.stdout.write(r.stderr)
    return r.stdout + r.stderr


def main():
    if sys.platform != "darwin":
        sys.exit("make_release_mac.py roda num Mac (codesign, ditto, lipo, strip): na CI, o job do macOS")
    ap = argparse.ArgumentParser(description="o zip do macOS")
    ap.add_argument("--app", default=os.path.join(ROOT, "build", "Nightfire PC.app"))
    ap.add_argument("--version", default=None)
    ap.add_argument("--out", default=None)
    ap.add_argument("--dsym", default=None, help="a pasta do dSYM (fora do zip); sem ela, nenhum")
    ap.add_argument("--allow-thin", action="store_true", help="aceita um executável de uma arquitetura só")
    a = ap.parse_args()

    app_src = os.path.abspath(a.app)
    info = plistlib.load(open(os.path.join(app_src, "Contents", "Info.plist"), "rb"))
    exe_name = info["CFBundleExecutable"]
    version = a.version or make_release.git("describe", "--tags", "--always") or info.get("CFBundleShortVersionString", "dev")
    # A etiqueta vX.Y vira X.Y no nome do zip, como o do Windows (Nightfire-PC-0.4.zip ao lado do Nightfire-PC-mac-0.4.zip).
    if re.match(r"v\d", version):
        version = version[1:]
    out_dir = a.out or os.path.join(make_release.main_checkout(), "out", "release")
    stage_root = os.path.join(out_dir, "stage-mac")
    stage = os.path.join(stage_root, "Nightfire PC")
    if os.path.isdir(stage_root):
        shutil.rmtree(stage_root)
    os.makedirs(stage)

    app = os.path.join(stage, "Nightfire PC.app")
    run("ditto", app_src, app)
    exe = os.path.join(app, "Contents", "MacOS", exe_name)
    archs = run("lipo", "-archs", exe, capture=True).split()
    print("arquiteturas: " + " ".join(archs))
    if not a.allow_thin and not ("arm64" in archs and "x86_64" in archs):
        sys.exit("o executável não é universal (arm64 e x86_64): " + " ".join(archs))
    if a.dsym:
        os.makedirs(a.dsym, exist_ok=True)
        run("dsymutil", exe, "-o", os.path.join(a.dsym, "Nightfire PC.app.dSYM"))
    run("strip", "-S", exe)
    # O hd_ui do pacote é a lista do zip do Windows (make_release.tracked_hd_ui), nem um arquivo a mais nem a menos: a
    # pasta de uma build antiga pode guardar um arquivo que saiu do git (o CMake não apaga recursos velhos do pacote).
    tracked = make_release.tracked_hd_ui()
    res = os.path.join(app, "Contents", "Resources")
    built = set()
    for dirpath, _, filenames in os.walk(os.path.join(res, "hd_ui")):
        for f in filenames:
            built.add(os.path.relpath(os.path.join(dirpath, f), res).replace(os.sep, "/"))
    extra = sorted(built - set(tracked))
    missing = sorted(set(tracked) - built)
    print("hd_ui: %d arquivos do git; o pacote da build tinha %d a mais e %d a menos" % (len(tracked), len(extra), len(missing)))
    for p in extra:
        print("  a mais (sai): " + p)
    for p in missing:
        print("  a menos (entra): " + p)
    shutil.rmtree(os.path.join(res, "hd_ui"), ignore_errors=True)
    for p in tracked:
        dst = os.path.join(res, *p.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(os.path.join(ROOT, *p.split("/")), dst)
    run("plutil", "-lint", os.path.join(app, "Contents", "Info.plist"))
    for need in ("Contents/Resources/nightfire.icns", "Contents/Resources/hd_ui/map.json",
                 "Contents/Resources/hd_ui/design/tokens.json", "Contents/Resources/hd_ui/fonts_ofl/rajdhani/OFL.txt"):
        if not os.path.isfile(os.path.join(app, *need.split("/"))):
            sys.exit("falta no pacote: " + need)
    # A assinatura ad hoc e a conferência (a do Gatekeeper, spctl, recusa: não há Developer ID).
    run("codesign", "--force", "--deep", "-s", "-", app)
    run("codesign", "--verify", "--deep", "--strict", "--verbose=2", app)
    print(run("codesign", "-dv", app, capture=True).strip())

    shutil.copy2(os.path.join(ROOT, "tools", "release", "README-mac.txt"), os.path.join(stage, "README.txt"))
    shutil.copy2(os.path.join(ROOT, "tools", "release", "LICENSES-mac.txt"), os.path.join(stage, "LICENSES.txt"))

    secrets = [("o nome da conta do git", make_release.git("config", "user.name")),
               ("o e-mail da conta do git", make_release.git("config", "user.email"))]
    home = os.path.expanduser("~").rstrip("/")
    if home.startswith("/Users/") and len(home) > len("/Users/"):
        secrets.append(("a pasta do usuário do Mac", home + "/"))
    problems = make_release.check_tree(stage, secrets, large_ok=make_release.LARGE_OK + [exe_name.lower()])
    if problems:
        for p in problems:
            print("PROBLEMA: " + p)
        sys.exit("o zip não foi feito")

    zip_path = os.path.join(out_dir, "Nightfire-PC-mac-%s.zip" % version)
    if os.path.exists(zip_path):
        os.remove(zip_path)
    run("ditto", "-c", "-k", "--keepParent", stage, zip_path)
    total = 0
    count = 0
    for dirpath, _, filenames in os.walk(stage):
        for f in sorted(filenames):
            full = os.path.join(dirpath, f)
            size = os.path.getsize(full)
            total += size
            count += 1
            print("%12d  %s" % (size, os.path.relpath(full, stage_root)))
    shutil.rmtree(stage_root)
    print("%d arquivos, %d bytes descomprimidos" % (count, total))
    print("zip: %s (%d bytes)" % (zip_path, os.path.getsize(zip_path)))


if __name__ == "__main__":
    main()
