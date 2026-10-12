"""O zip de lançamento do port (docs/port.md, "Instalação fácil").

Monta, a partir das saídas da compilação, uma pasta "Nightfire PC" e o zip dela:
  nightfire.exe + nightfire.pdb          a versão de 32 bits (build\\Release; a de referência, o padrão)
  nightfire64.exe + nightfire64.pdb      a de 64 bits (build\\x64\\Release; experimental), se existir
  msvcp140.dll, vcruntime140.dll         o runtime do Visual C++ que a de 32 bits usa (a de 64 bits o tem dentro)
  hd_ui\\                                 as fontes (Rajdhani, OFL), os SVG e os tokens do estilo Modern e o map.json
                                         (só os arquivos que o git conhece: hd_ui\\fonts, do usuário, fica de fora)
  README.txt, LICENSES.txt               tools/release/

Nada do jogo entra: o script confere a lista (nenhum .ELF, FILES.BIN, .PSS, .ISO, .IRX, nada das pastas do disco) e
procura no conteúdo o nome e o e-mail da conta do git e o nome do usuário do Windows (os PDB guardam caminhos da
compilação; o build.bat põe os temporários do linker numa pasta do build), sem nunca escrevê-los. O zip vai para out/release/ da cópia principal
do repositório (git-ignorado); nada é publicado.

Uso:
  python tools/make_release.py [--version 0.2] [--out <pasta>] [--x86 build/Release] [--x64 build/x64/Release]
                               [--no-x64] [--redist <pasta do Microsoft.VC145.CRT x86>] [--allow-user-name]
"""

import argparse
import fnmatch
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Nada com estes nomes ou extensões (os arquivos do disco e o que se tira deles).
FORBIDDEN_NAMES = ["*.elf", "files.bin", "*.pss", "*.iso", "*.irx", "*.img", "system.cnf", "slus_*", "*.viv", "*.mus",
                   "*.spe", "*.sym", "*.sbf", "*.shf", "*.sfx", "*.sbi", "*.mxi", "*.smf", "*.ssd", "*.lut", "*.dds",
                   "*.bin", "*.icn"]
FORBIDDEN_DIRS = ["ps2", "movies", "driving", "modules", "extracted", "textures", "saves", "out"]
# Os arquivos que podem passar de 4 MB (o resto é texto, fontes e SVG).
LARGE_OK = ["*.exe", "*.pdb"]


def git(*args):
    r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    return r.stdout.strip() if r.returncode == 0 else ""


def main_checkout():
    """A pasta da cópia principal do repositório (numa worktree, a de cima do .git comum)."""
    common = git("rev-parse", "--path-format=absolute", "--git-common-dir")
    if common:
        return os.path.dirname(os.path.normpath(common))
    return ROOT


def find_redist():
    base = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Redist\MSVC"
    if not os.path.isdir(base):
        return None
    for ver in sorted(os.listdir(base), reverse=True):
        x86 = os.path.join(base, ver, "x86")
        if not os.path.isdir(x86):
            continue
        for d in os.listdir(x86):
            if d.startswith("Microsoft.VC") and d.endswith(".CRT"):
                return os.path.join(x86, d)
    return None


def check_tree(stage, secrets):
    problems = []
    for dirpath, dirnames, filenames in os.walk(stage):
        rel_dir = os.path.relpath(dirpath, stage)
        for d in dirnames:
            if d.lower() in FORBIDDEN_DIRS:
                problems.append("pasta proibida: " + os.path.join(rel_dir, d))
        for f in filenames:
            rel = os.path.normpath(os.path.join(rel_dir, f))
            low = f.lower()
            if any(fnmatch.fnmatch(low, p) for p in FORBIDDEN_NAMES):
                problems.append("arquivo proibido: " + rel)
            size = os.path.getsize(os.path.join(dirpath, f))
            if size > (4 << 20) and not any(fnmatch.fnmatch(low, p) for p in LARGE_OK):
                problems.append("arquivo grande demais para ser do port: %s (%d bytes)" % (rel, size))
            data = open(os.path.join(dirpath, f), "rb").read()
            for label, s in secrets:
                for enc in ("utf-8", "utf-16-le"):
                    if s and s.encode(enc) in data:
                        problems.append("%s aparece em %s" % (label, rel))
    return problems


def main():
    ap = argparse.ArgumentParser(description="o zip de lançamento do port")
    ap.add_argument("--version", default=None)
    ap.add_argument("--out", default=None)
    ap.add_argument("--x86", default=os.path.join(ROOT, "build", "Release"))
    ap.add_argument("--x64", default=os.path.join(ROOT, "build", "x64", "Release"))
    ap.add_argument("--no-x64", action="store_true")
    ap.add_argument("--redist", default=None)
    ap.add_argument("--allow-user-name", action="store_true")
    a = ap.parse_args()

    version = a.version or git("describe", "--tags", "--always") or "dev"
    out_dir = a.out or os.path.join(main_checkout(), "out", "release")
    stage_root = os.path.join(out_dir, "stage")
    stage = os.path.join(stage_root, "Nightfire PC")
    if os.path.isdir(stage_root):
        shutil.rmtree(stage_root)
    os.makedirs(stage)

    def copy(src, name):
        if not os.path.isfile(src):
            sys.exit("falta " + src + " (compile antes: build.bat Release / build.bat Release x64)")
        shutil.copy2(src, os.path.join(stage, name))

    copy(os.path.join(a.x86, "nightfire.exe"), "nightfire.exe")
    copy(os.path.join(a.x86, "nightfire.pdb"), "nightfire.pdb")
    if not a.no_x64:
        copy(os.path.join(a.x64, "nightfire.exe"), "nightfire64.exe")
        copy(os.path.join(a.x64, "nightfire64.pdb"), "nightfire64.pdb")
    redist = a.redist or find_redist()
    if redist is None:
        sys.exit("não achei o runtime do Visual C++ (x86) do Build Tools; use --redist")
    for dll in ("msvcp140.dll", "vcruntime140.dll"):
        copy(os.path.join(redist, dll), dll)
    # O hd_ui que o git conhece (a pasta fonts do usuário fica de fora).
    tracked = [p for p in git("ls-files", "hd_ui").splitlines() if p]
    if not tracked:
        sys.exit("git ls-files hd_ui não deu nada")
    for p in tracked:
        dst = os.path.join(stage, *p.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(os.path.join(ROOT, *p.split("/")), dst)
    for name in ("README.txt", "LICENSES.txt"):
        copy(os.path.join(ROOT, "tools", "release", name), name)

    secrets = [("o nome da conta do git", git("config", "user.name")),
               ("o e-mail da conta do git", git("config", "user.email"))]
    # O nome do usuário do Windows: os PDB guardam os caminhos da compilação (os temporários do linker no %TEMP%, a pasta
    # do repositório). O build.bat põe os temporários numa pasta do build; um repositório dentro da pasta do usuário
    # ainda o traria (--allow-user-name aceita).
    user = os.environ.get("USERNAME", "")
    if user and not a.allow_user_name:
        secrets.append(("o nome do usuário do Windows (compile com o build.bat, fora da pasta do usuário)", "\\" + user + "\\"))
    problems = check_tree(stage, secrets)
    if problems:
        for p in problems:
            print("PROBLEMA: " + p)
        sys.exit("o zip não foi feito")

    zip_path = os.path.join(out_dir, "Nightfire-PC-%s.zip" % version)
    if os.path.exists(zip_path):
        os.remove(zip_path)
    total = 0
    listing = []
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dirpath, _, filenames in os.walk(stage):
            for f in sorted(filenames):
                full = os.path.join(dirpath, f)
                arc = os.path.relpath(full, stage_root).replace(os.sep, "/")
                z.write(full, arc)
                size = os.path.getsize(full)
                total += size
                listing.append((arc, size))
    shutil.rmtree(stage_root)
    for arc, size in sorted(listing):
        print("%12d  %s" % (size, arc))
    print("%d arquivos, %d bytes descomprimidos" % (len(listing), total))
    print("zip: %s (%d bytes)" % (zip_path, os.path.getsize(zip_path)))


if __name__ == "__main__":
    main()
