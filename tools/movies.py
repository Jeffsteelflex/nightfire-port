"""Os filmes do jogo (MOVIES/30_FPS/*.PSS): lista, separa o vídeo e o áudio, confere o demultiplexador.

Formato em docs/formats.md, "Vídeos". Resumo: um PSS é um "program stream" MPEG-2 (pacotes 0x000001BA de 16 KB),
com o vídeo MPEG-2 no stream 0xE0 (512x448, 30 quadros por segundo, 4:3, Main Profile @ Main Level, codificado com o
TMPGEnc) e o áudio no "private stream 1" (0xBD): cada PES começa com 4 bytes (0xff, o sub-stream 0xa0, 0, 0) e o que
segue, junto, é o formato da Sony "SShd"/"SSbd": um cabeçalho de 0x20 bytes (tipo 1 = PCM de 16 bits little-endian,
48 kHz, 2 canais, intercalação 0x200) e o corpo, em blocos de 0x200 bytes alternados (esquerdo, direito). É o formato
que a SPU2 lê na entrada de dados do núcleo 0 (a transferência em bloco de `audioDecStart`).

Uso (a raiz dos dados é --root, padrão: a raiz do repositório; os arquivos ficam em <raiz>/MOVIES/30_FPS):
    python tools/movies.py list                      os 22 filmes: tamanho, imagem, quadros, duração, áudio, onde tocam
    python tools/movies.py info <filme>              pacotes, streams, GOPs e tipos de quadro de um filme (hash ou caminho)
    python tools/movies.py demux <filme> <pasta>     grava <hash>.m2v (o vídeo elementar) e <hash>.wav (o áudio)
    python tools/movies.py test                      confere o demultiplexador com um stream montado à mão

O que sai daqui (.m2v, .wav) é dado do jogo: grave em out/.
"""
import argparse
import os
import struct
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Onde o jogo toca cada filme, com o nome da página de depuração P_FMVTEST_Handler (ACTION.ELF: UsingMovie lista os 23
# códigos; 07360048 não está no disco e strFileOpen trava o PS2 se ele faltar). Os códigos 071xxxxx e 073xxxxx vêm de ResetMap_LevelToLoad (a fase + 0x100000, com o
# filme de abertura) e de Mission_Update (LevelToEndTo); os 073x0048 das páginas do menu principal.
WHERE = {
    0x07100001: "Estate - Meet Mayhew: abertura da missão 07000001 (ResetMap_Load; legendas: script 0600095a)",
    0x07100005: "Castle - Halo Jump: abertura da missão 07000005 (script 06000961)",
    0x07100009: "Tower Infil - Intro: abertura da missão 07000009 (script 06000963)",
    0x0710000C: "Nuclear Plant - Interview: abertura da missão 0700000c (script 06000965)",
    0x07100011: "Tower Escape - Dom Death: abertura da missão 07000011 (script 06000967)",
    0x07100014: "Evil Base - Descent All: abertura da missão 07000014 (script 06000969)",
    0x07100018: "Space Station - Arrival: abertura da missão 0700001b (ResetMap_LevelToLoad troca 0700001b por "
                "07100018; script 0600096b)",
    0x07300004: "Estate - Mayhew Death: fim da missão 07000001 (Mission_Update, LevelToEndTo; depois os resultados; "
                "script 0600095d)",
    0x07300008: "Castle - Gondola: fim da missão 07000005 (script 06000962)",
    0x0730000B: "Tower Infil - Outro: fim da missão 07000009 (script 06000964)",
    0x0730000D: "Nuclear Plant - Entering Vent: fim da missão 0700000c (script 06000966)",
    0x07300013: "Tower Escape - Lobby Rescue: fim da missão 07000011 (script 06000968)",
    0x07300017: "Evil Base - Kiko Death: fim da missão 07000014 (script 0600097a)",
    0x0730001B: "Arrival - Happy Ending: fim da missão 0700001b (script 0600096c)",
    0x07330048: "Menu - BG Loop: fundo do menu principal 0x40000002 (laço, Menu_PlayMovie pela página)",
    0x07350048: "Menu - FE Loop: fundo das outras páginas do menu principal (laço; 40 páginas de 08000002)",
    0x07370048: "Menu - EA Logo: abertura do jogo, primeiro (P_INTRO_Handler; não pula)",
    0x07380048: "Menu - MGM Logo: abertura do jogo, segundo (P_INTRO_Handler; não pula)",
    0x07390048: "Menu - DAD Trailer: o trailer de Die Another Day (P_TRAILER_Handler, AV Options)",
    0x073A0048: "Menu - Attract Movie: demonstração (P_ATTRACT_Handler, 45 s sem entrada)",
    0x073B0048: "Menu - Esthero: o clipe (P_ESTHERO_Handler depois do Press START, pula depois de 180 ticks; e a "
                "demonstração do menu principal, uma vez sim, outra não)",
    0x073F0048: "Menu - Making Of: o making of, no fim do jogo (P_WINGAME_Handler, depois os créditos)",
}

FRAME_RATES = {1: 24000 / 1001, 2: 24.0, 3: 25.0, 4: 30000 / 1001, 5: 30.0, 6: 50.0, 7: 60000 / 1001, 8: 60.0}
ASPECTS = {1: "1:1", 2: "4:3", 3: "16:9", 4: "2.21:1"}


def movies_dir(root):
    return os.path.join(root, "MOVIES", "30_FPS")


def movie_path(root, arg):
    if os.path.exists(arg):
        return arg
    return os.path.join(movies_dir(root), "%08X.PSS" % int(arg, 16))


# ---- demultiplexador ------------------------------------------------------------------------------

def parse_pts(b):
    return ((b[0] >> 1) & 7) << 30 | b[1] << 22 | (b[2] >> 1) << 15 | b[3] << 7 | b[4] >> 1


def iter_pes(d):
    """Percorre um program stream MPEG-2 (ou MPEG-1). Devolve (posição, stream, pts ou None, payload) de cada PES de
    vídeo (0xE0-0xEF), áudio MPEG (0xC0-0xDF) e private stream 1 (0xBD); conta os outros em stats."""
    pos = 0
    n = len(d)
    stats = {"packs": 0, "system": 0, "padding": 0, "end": None, "other": 0}
    while pos + 4 <= n:
        if d[pos:pos + 3] != b"\x00\x00\x01":
            raise ValueError("sem código de início em 0x%x: %s" % (pos, d[pos:pos + 8].hex()))
        code = d[pos + 3]
        if code == 0xBA:
            stats["packs"] += 1
            if d[pos + 4] >> 6 == 1:  # MPEG-2: 10 bytes + enchimento
                pos += 14 + (d[pos + 13] & 7)
            else:  # MPEG-1: 8 bytes
                pos += 12
            continue
        if code == 0xB9:
            stats["end"] = pos
            pos += 4
            break
        ln = struct.unpack_from(">H", d, pos + 4)[0]
        body = d[pos + 6:pos + 6 + ln]
        if code == 0xBB:
            stats["system"] += 1
        elif code == 0xBE:
            stats["padding"] += 1
        elif code == 0xBD or 0xC0 <= code <= 0xEF:
            pts = None
            if body[0] >> 6 == 2:  # cabeçalho PES do MPEG-2
                flags, hl = body[1], body[2]
                if flags & 0x80:
                    pts = parse_pts(body[3:8])
                payload = body[3 + hl:]
            else:  # MPEG-1: enchimento 0xff, STD, PTS
                i = 0
                while body[i] == 0xFF:
                    i += 1
                if body[i] >> 6 == 1:
                    i += 2
                if body[i] >> 4 in (2, 3):
                    pts = parse_pts(body[i:i + 5])
                    i += 10 if body[i] >> 4 == 3 else 5
                else:
                    i += 1
                payload = body[i:]
            yield pos, code, pts, payload
        else:
            stats["other"] += 1
        pos += 6 + ln
    iter_pes.stats = stats


def demux(d):
    """Separa o vídeo elementar (stream 0xE0) e o áudio do private stream 1 (sub-stream 0xa0, sem os 4 bytes de
    cabeçalho de cada PES). Devolve um dicionário com os bytes e as contas."""
    video = bytearray()
    audio = bytearray()
    first_video_pts = first_audio_pts = None
    nvid = naud = 0
    subs = {}
    for _pos, code, pts, payload in iter_pes(d):
        if code == 0xE0:
            if first_video_pts is None and pts is not None:
                first_video_pts = pts
            video += payload
            nvid += 1
        elif code == 0xBD:
            sub = payload[1] if len(payload) > 1 else None
            subs[sub] = subs.get(sub, 0) + 1
            if sub != 0xA0:
                continue
            if first_audio_pts is None and pts is not None:
                first_audio_pts = pts
            audio += payload[4:]
            naud += 1
    return {"video": bytes(video), "audio": bytes(audio), "video_pes": nvid, "audio_pes": naud,
            "video_pts": first_video_pts, "audio_pts": first_audio_pts, "subs": subs, "stats": dict(iter_pes.stats)}


# ---- vídeo elementar ------------------------------------------------------------------------------

def start_codes(es):
    i = es.find(b"\x00\x00\x01")
    while i >= 0 and i + 3 < len(es):
        yield i, es[i + 3]
        i = es.find(b"\x00\x00\x01", i + 3)


def video_info(es):
    info = {"pictures": 0, "gops": 0, "sequences": 0, "types": {"I": 0, "P": 0, "B": 0}}
    for i, code in start_codes(es):
        if code == 0x00:
            info["pictures"] += 1
            ctype = (es[i + 5] >> 3) & 7
            info["types"][{1: "I", 2: "P", 3: "B"}.get(ctype, "?")] = info["types"].get({1: "I", 2: "P", 3: "B"}.get(ctype, "?"), 0) + 1
        elif code == 0xB8:
            info["gops"] += 1
        elif code == 0xB3:
            info["sequences"] += 1
            if "width" not in info:
                b = es[i + 4:i + 12]
                info["width"] = (b[0] << 4) | (b[1] >> 4)
                info["height"] = ((b[1] & 0xF) << 8) | b[2]
                info["aspect"] = ASPECTS.get(b[3] >> 4, "?")
                info["fps"] = FRAME_RATES.get(b[3] & 0xF, 0.0)
                info["bitrate"] = ((b[4] << 10) | (b[5] << 2) | (b[6] >> 6)) * 400
                info["vbv"] = (((b[6] & 0x1F) << 5) | (b[7] >> 3)) * 2048
                info["intra_matrix"] = bool(b[7] & 2)
                # Sem a matriz intra, o bit seguinte diz se vem a não-intra (com ela, só 64 bytes adiante).
                info["non_intra_matrix"] = bool(b[7] & 1) if not b[7] & 2 else bool(es[i + 11 + 64] & 1)
        elif code == 0xB5 and "profile" not in info and es[i + 4] >> 4 == 1:
            b = es[i + 4:i + 10]
            info["profile"] = {4: "Main", 5: "Simple", 1: "High"}.get(b[0] & 7, "?") + "@" + \
                {8: "Main", 10: "Low", 6: "High-1440", 4: "High"}.get(b[1] >> 4, "?")
            info["progressive"] = bool(b[1] & 8)
            info["chroma"] = {1: "4:2:0", 2: "4:2:2", 3: "4:4:4"}.get((b[1] >> 1) & 3, "?")
    return info


# ---- áudio SShd/SSbd ------------------------------------------------------------------------------

def parse_sshd(audio):
    """O cabeçalho da Sony: 'SShd' tamanho(0x18) tipo taxa canais intercalação laço_início laço_fim, depois 'SSbd'
    tamanho. Devolve (cabeçalho, deslocamento do corpo)."""
    if audio[:4] != b"SShd":
        raise ValueError("áudio sem SShd: %s" % audio[:8].hex())
    size = struct.unpack_from("<I", audio, 4)[0]
    typ, rate, ch, inter, loop_s, loop_e = struct.unpack_from("<6i", audio, 8)
    body = 8 + size
    if audio[body:body + 4] != b"SSbd":
        raise ValueError("áudio sem SSbd em 0x%x" % body)
    body_size = struct.unpack_from("<I", audio, body + 4)[0]
    return {"type": typ, "rate": rate, "channels": ch, "interleave": inter, "loop": (loop_s, loop_e),
            "body_size": body_size}, body + 8


def deinterleave(body, channels, interleave):
    """Os blocos de `interleave` bytes de cada canal, em ordem, viram amostras estéreo intercaladas (16 bits LE). Um
    resto que não fecha um conjunto de blocos é descartado (como o fim do buffer na SPU2)."""
    group = interleave * channels
    whole = len(body) // group
    out = bytearray(whole * group)
    per = interleave // 2
    for g in range(whole):
        base = g * group
        blocks = [body[base + c * interleave:base + (c + 1) * interleave] for c in range(channels)]
        for c in range(channels):
            b = blocks[c]
            for k in range(per):
                o = ((g * per + k) * channels + c) * 2
                out[o:o + 2] = b[2 * k:2 * k + 2]
    return bytes(out)


def audio_info(audio):
    if not audio:  # os fundos do menu (07330048, 07350048) não têm áudio
        return {"type": None, "rate": 0, "channels": 0, "interleave": 0, "loop": None, "body_size": 0, "frames": 0,
                "seconds": 0.0, "present": 0}
    hdr, off = parse_sshd(audio)
    body = audio[off:off + hdr["body_size"]]
    frames = len(body) // (2 * hdr["channels"]) if hdr["type"] == 1 else 0
    hdr["frames"] = frames
    hdr["seconds"] = frames / hdr["rate"] if hdr["rate"] else 0.0
    hdr["present"] = len(audio) - off
    return hdr


# ---- comandos -------------------------------------------------------------------------------------

def load(path):
    with open(path, "rb") as f:
        return f.read()


def cmd_list(args):
    folder = movies_dir(args.root)
    names = sorted(n for n in os.listdir(folder) if n.upper().endswith(".PSS"))
    print("%-8s %5s %9s %5s %7s %5s %4s %8s %8s  %s" %
          ("filme", "MB", "imagem", "fps", "quadros", "I", "B", "vídeo s", "áudio s", "onde"))
    total = 0.0
    for name in names:
        d = load(os.path.join(folder, name))
        r = demux(d)
        v = video_info(r["video"])
        a = audio_info(r["audio"])
        secs = v["pictures"] / v["fps"]
        total += secs
        code = int(os.path.splitext(name)[0], 16)
        print("%08x %5.1f %4dx%-4d %5.2f %7d %5d %4d %8.2f %8s  %s" %
              (code, len(d) / 1e6, v["width"], v["height"], v["fps"], v["pictures"], v["types"]["I"], v["types"]["B"],
               secs, "%.2f" % a["seconds"] if a["type"] is not None else "-", WHERE.get(code, "?")))
    print("%d filmes, %.1f minutos" % (len(names), total / 60))


def cmd_info(args):
    path = movie_path(args.root, args.movie)
    d = load(path)
    r = demux(d)
    st = r["stats"]
    v = video_info(r["video"])
    a = audio_info(r["audio"])
    print("%s: %d bytes, %d pacotes de %d bytes, fim 0x%x" % (path, len(d), st["packs"], len(d) // max(1, st["packs"]),
                                                           st["end"] or -1))
    print("PES: vídeo %d, áudio %d (sub-streams %s), cabeçalho de sistema %d, enchimento %d" %
          (r["video_pes"], r["audio_pes"], {("0x%02x" % k if k is not None else None): n for k, n in r["subs"].items()},
           st["system"], st["padding"]))
    print("vídeo: %dx%d %s %.3f fps, %s %s %s, taxa %d bit/s, VBV %d bytes, matrizes próprias intra %s não-intra %s" %
          (v["width"], v["height"], v["aspect"], v["fps"], v.get("profile"), v.get("chroma"),
           "progressivo" if v.get("progressive") else "entrelaçado", v["bitrate"], v["vbv"], v["intra_matrix"],
           v["non_intra_matrix"]))
    print("       %d bytes, %d quadros (%s), %d GOPs, %d cabeçalhos de sequência, %.2f s, PTS inicial %s" %
          (len(r["video"]), v["pictures"], " ".join("%s %d" % kv for kv in v["types"].items()), v["gops"],
           v["sequences"], v["pictures"] / v["fps"], r["video_pts"]))
    if a["type"] is None:
        print("áudio: nenhum")
    else:
        print("áudio: tipo %d (%s), %d Hz, %d canais, intercalação 0x%x, laço %s, corpo %d bytes (%d presentes), "
              "%d amostras, %.2f s, PTS inicial %s" %
              (a["type"], "PCM 16 bits LE" if a["type"] == 1 else "?", a["rate"], a["channels"], a["interleave"],
               a["loop"], a["body_size"], a["present"], a["frames"], a["seconds"], r["audio_pts"]))
    code = os.path.splitext(os.path.basename(path))[0]
    print("onde: %s" % WHERE.get(int(code, 16), "?"))


def cmd_demux(args):
    path = movie_path(args.root, args.movie)
    d = load(path)
    r = demux(d)
    os.makedirs(args.dir, exist_ok=True)
    code = os.path.splitext(os.path.basename(path))[0].lower()
    m2v = os.path.join(args.dir, code + ".m2v")
    with open(m2v, "wb") as f:
        f.write(r["video"])
    if not r["audio"]:
        print("%s: %d bytes de vídeo, sem áudio" % (m2v, len(r["video"])))
        return
    hdr, off = parse_sshd(r["audio"])
    body = r["audio"][off:off + hdr["body_size"]]
    pcm = deinterleave(body, hdr["channels"], hdr["interleave"])
    wav = os.path.join(args.dir, code + ".wav")
    with wave.open(wav, "wb") as w:
        w.setnchannels(hdr["channels"])
        w.setsampwidth(2)
        w.setframerate(hdr["rate"])
        w.writeframes(pcm)
    print("%s: %d bytes de vídeo, %s: %d amostras (%.2f s)" %
          (m2v, len(r["video"]), wav, len(pcm) // (2 * hdr["channels"]), len(pcm) / (2 * hdr["channels"]) / hdr["rate"]))


# ---- teste ----------------------------------------------------------------------------------------

def build_pack(pes_list):
    """Um pacote MPEG-2 com os PES dados (stream, pts, payload), como o do PSS: cabeçalho de pacote com 4 bytes de
    enchimento, PES com PTS e 5 bytes de enchimento no cabeçalho."""
    out = bytearray(b"\x00\x00\x01\xba\x44\x00\x04\x00\x04\x01\x00\xd8\x43\xfc\xff\xff\xff\xff")
    for code, pts, payload in pes_list:
        hdr = bytearray([0x83, 0x80 if pts is not None else 0x00, 0x0A if pts is not None else 0x05])
        if pts is not None:
            hdr += bytes([0x21 | ((pts >> 29) & 0xE), (pts >> 22) & 0xFF, ((pts >> 14) & 0xFE) | 1,
                          (pts >> 7) & 0xFF, ((pts << 1) & 0xFE) | 1])
        hdr += b"\xff" * 5
        body = bytes(hdr) + payload
        out += b"\x00\x00\x01" + bytes([code]) + struct.pack(">H", len(body)) + body
    return bytes(out)


def cmd_test(_args):
    # Um áudio de 2 canais com intercalação 4 (2 amostras por bloco): L0 L1 | R0 R1 | L2 L3 | R2 R3.
    sshd = b"SShd" + struct.pack("<I6i", 0x18, 1, 48000, 2, 4, -1, -1)
    samples = [(100, -100), (200, -200), (300, -300), (400, -400)]
    body = struct.pack("<2h", samples[0][0], samples[1][0]) + struct.pack("<2h", samples[0][1], samples[1][1]) + \
        struct.pack("<2h", samples[2][0], samples[3][0]) + struct.pack("<2h", samples[2][1], samples[3][1])
    audio = sshd + b"SSbd" + struct.pack("<I", len(body)) + body
    # O vídeo: um cabeçalho de sequência de 512x448 4:3 a 30 fps e dois quadros (I, P), divididos entre dois PES.
    seq = b"\x00\x00\x01\xb3\x20\x01\xc0\x25\x09\xc4\x22\x80"
    pic_i = b"\x00\x00\x01\x00\x00\x0f\xff\xf8"
    pic_p = b"\x00\x00\x01\x00\x00\x50\x00\x00"
    video = seq + pic_i + b"\x11" * 7 + pic_p + b"\x22" * 5
    half = len(video) // 2
    # O áudio em dois PES, cortado no meio de uma amostra (como no disco: 4073 bytes por PES).
    cut = 41
    d = build_pack([(0xE0, 6360, video[:half]), (0xBD, 6360, b"\xff\xa0\x00\x00" + audio[:cut])]) + \
        build_pack([(0xBD, None, b"\xff\xa0\x00\x00" + audio[cut:]), (0xE0, None, video[half:]),
                    (0xBD, None, b"\xff\xa1\x00\x00" + b"outro")]) + b"\x00\x00\x01\xb9"
    r = demux(d)
    assert r["video"] == video and r["audio"] == audio, (r["video"], r["audio"])
    assert r["video_pts"] == 6360 and r["audio_pts"] == 6360 and r["subs"] == {0xA0: 2, 0xA1: 1}, r
    assert r["stats"]["packs"] == 2 and r["stats"]["end"] == len(d) - 4
    v = video_info(r["video"])
    assert (v["width"], v["height"], v["aspect"], v["fps"], v["pictures"]) == (512, 448, "4:3", 30.0, 2), v
    assert v["types"]["I"] == 1 and v["types"]["P"] == 1, v
    hdr, off = parse_sshd(r["audio"])
    assert (hdr["type"], hdr["rate"], hdr["channels"], hdr["interleave"]) == (1, 48000, 2, 4), hdr
    pcm = deinterleave(r["audio"][off:off + hdr["body_size"]], 2, 4)
    assert list(struct.unpack("<8h", pcm)) == [x for s in samples for x in s], struct.unpack("<8h", pcm)
    print("ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=ROOT, help="pasta com os dados do disco (MOVIES/)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    p = sub.add_parser("info"); p.add_argument("movie")  # noqa: E702
    p = sub.add_parser("demux"); p.add_argument("movie"); p.add_argument("dir")  # noqa: E702
    sub.add_parser("test")
    args = ap.parse_args()
    globals()["cmd_" + args.cmd](args)


if __name__ == "__main__":
    main()
