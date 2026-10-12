"""Roda o --bench do nightfire.exe numa matriz de cenas, resoluções, adaptadores e modos e monta uma tabela.

Uso (da raiz do repositório):
    python tools/bench_matrix.py --exe build/Release/nightfire.exe --data "C:/Projects/Nightifire Port" \
        --out out/bench/matriz.md [--seconds 10] [--adapters integrated,discrete] [--res 1920x1080,1280x720] \
        [--modes offscreen,window] [--scenes 0700000c,07000012] [--extra "--no-hd"]

Cada linha da tabela é uma execução: a cena (nome da lista SCENES), o adaptador, a resolução, o modo (fora da tela =
o custo da GPU; janela = com o Present e, no integrado com o monitor na outra placa, a cópia entre placas), o fps
médio, o 1% e o 0,1% low (percentis 99 e 99,9 do tempo de quadro), o tempo de GPU por quadro (carimbos de tempo do
Direct3D 11) com os trechos mais caros, e os desenhos por quadro. A saída bruta de cada execução vai junto
(<out>.txt). Só a biblioteca padrão.
"""

import argparse
import os
import re
import subprocess
import sys

# As cenas: o que o --bench mede em cada uma (o --input gira o jogador para a câmera ver o nível todo).
TURN = "mlook=700,0:120"
SCENES = {
    # 0700000c: a fase com mais partes (~3000 desenhos por quadro), girando no lugar.
    "0700000c": ["--level", "0700000c", "--input", TURN],
    # 07000014 e 0700001b: as outras pesadas.
    "07000014": ["--level", "07000014", "--input", TURN],
    "0700001b": ["--level", "0700001b", "--input", TURN],
    # 07000012: o tiroteio do começo (os guardas atiram de ~6 s; parado e girando devagar, atirando).
    "07000012-tiros": ["--level", "07000012", "--wait", "5", "--input",
                       "none:5 mlook=250,0+lmb:2 mlook=-250,0+lmb:2 mlook=250,0+lmb:2 mlook=-250,0+lmb:2 mlook=250,0+lmb:2"],
    # 07000012 com a pausa Modern aberta (o desfoque da cena e o Direct2D a cada quadro).
    "07000012-pausa": ["--level", "07000012", "--open-menu-page", "4000004b", "--menu-tab", "pause", "--wait", "2.5"],
    # 07000012 com as opções Modern (a lista de VIDEO).
    "07000012-opcoes": ["--level", "07000012", "--open-menu-page", "40000060", "--menu-tab", "video", "--wait", "2.5"],
    # O menu principal Modern (Press START > o menu: a arte desfocada, o logotipo e o Direct2D).
    "menu": ["--input", "none:4.5 enter:0.1", "--wait", "7"],
    # A tela de título (a arte nítida, sem desfoque).
    "titulo": ["--wait", "2"],
}


def run(exe, data, scene, adapter, res, mode, seconds, extra):
    w, h = res.split("x")
    cmd = [os.path.abspath(exe), "--data", data, "--width", w, "--height", h, "--bench", str(seconds), "--adapter", adapter]
    if mode == "offscreen":
        cmd.append("--offscreen")
    cmd += SCENES[scene]
    cmd += extra
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    return cmd, p.stdout


def parse(text):
    r = {}
    m = re.search(r"adaptador (\d+) \"([^\"]*)\"", text)
    if m:
        r["gpu"] = m.group(2)
    m = re.search(r"(janela|fora_da_tela) (\d+)x(\d+)", text)
    if m:
        r["tam"] = m.group(2) + "x" + m.group(3)
    m = re.search(r"quadros (\d+) tempo ([\d.]+) s fps_medio ([\d.]+)", text)
    if m:
        r["fps"] = float(m.group(3))
    m = re.search(r"p99 ([\d.]+) \(1% low ([\d.]+) fps\) p99.9 ([\d.]+) \(0.1% low ([\d.]+) fps\)", text)
    if m:
        r["low1"] = float(m.group(2))
        r["low01"] = float(m.group(4))
    m = re.search(r"quadro_ms media ([\d.]+)", text)
    if m:
        r["ms"] = float(m.group(1))
    m = re.search(r"gpu_ms media ([\d.]+) max ([\d.]+) \|(.*?)\|", text)
    if m:
        r["gpu_ms"] = float(m.group(1))
        parts = re.findall(r"(\S+) ([\d.]+)", m.group(3))
        r["fases"] = {k: float(v) for k, v in parts}
    m = re.search(r"chamadas ([\d.]+)", text)
    if m:
        r["chamadas"] = float(m.group(1))
    m = re.search(r"envio ([\d.]+) present ([\d.]+)", text)
    if m:
        r["envio"] = float(m.group(1))
        r["present"] = float(m.group(2))
    return r


def gpu_label(r):
    g = r.get("gpu", r["adapter"])
    if "Radeon" in g and "Graphics" in g:
        return "iGPU (Radeon)"
    if "RTX 5090" in g:
        return "RTX 5090"
    return g


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--adapters", default="integrated,auto")
    ap.add_argument("--res", default="1920x1080,1280x720")
    ap.add_argument("--modes", default="offscreen,window")
    ap.add_argument("--scenes", default=",".join(SCENES))
    ap.add_argument("--extra", default="")
    a = ap.parse_args()
    extra = a.extra.split() if a.extra else []
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    rows = []
    raw = open(a.out + ".txt", "a", encoding="utf-8")
    for scene in a.scenes.split(","):
        for adapter in a.adapters.split(","):
            for res in a.res.split(","):
                for mode in a.modes.split(","):
                    cmd, out = run(a.exe, a.data, scene, adapter, res, mode, a.seconds, extra)
                    raw.write("# " + " ".join(cmd) + "\n" + out + "\n")
                    raw.flush()
                    r = parse(out)
                    r.update(scene=scene, adapter=adapter, res=res, mode=mode)
                    rows.append(r)
                    fases = r.get("fases", {})
                    top = sorted(fases.items(), key=lambda kv: -kv[1])[:3]
                    print("%-16s %-10s %-9s %-9s fps %7.1f  1%% %7.1f  0.1%% %7.1f  gpu %6.3f ms  %s" % (
                        scene, adapter, res, mode, r.get("fps", 0), r.get("low1", 0), r.get("low01", 0),
                        r.get("gpu_ms", 0), " ".join("%s %.2f" % kv for kv in top)), flush=True)
    raw.close()
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("| cena | GPU | resolução | modo | fps médio | 1% low | 0.1% low | GPU ms/quadro | trechos mais caros (ms) | desenhos |\n")
        f.write("|---|---|---|---|---:|---:|---:|---:|---|---:|\n")
        for r in rows:
            fases = r.get("fases", {})
            top = sorted(fases.items(), key=lambda kv: -kv[1])[:3]
            f.write("| %s | %s | %s | %s | %.0f | %.0f | %.0f | %.2f | %s | %.0f |\n" % (
                r["scene"], gpu_label(r),
                r["res"], "fora da tela" if r["mode"] == "offscreen" else "janela", r.get("fps", 0), r.get("low1", 0),
                r.get("low01", 0), r.get("gpu_ms", 0), ", ".join("%s %.2f" % kv for kv in top), r.get("chamadas", 0)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
