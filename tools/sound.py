"""Os sons do jogo: bancos de efeitos (PS2/<LÍNGUA>/SB_*), falas em streaming (STREAMS.BIN/.LUT), música
(PS2/MUSIC/MFX_*) e a tabela de saída dos sons no ACTION.ELF. Formatos em docs/formats.md, "Áudio".

O driver de som é o módulo do IOP SFX.IRX (EuroSound da Eurocom, "Q:/SS/source/SFX/SFX_Main.cpp"), com
.symtab e STABS: as estruturas abaixo vêm de lá (SFXParameters, SFXSamplePoolFiles, SampleHeaderData,
StreamMarkerHeaderData, MusicMarkerData). As amostras são PS-ADPCM (VAG sem cabeçalho) da SPU2.

Uso (a raiz dos dados é --root, padrão: a raiz do repositório):
    python tools/sound.py banks                       os 25 bancos (SBINFO.SBI) e o que cada fase carrega
    python tools/sound.py sfx <banco>                 as definições de som (SFX) de um banco (0-24 ou o hash)
    python tools/sound.py samples <banco>             os cabeçalhos das amostras (SHF)
    python tools/sound.py wav <banco> <amostra> <saída.wav> [--loops N]
    python tools/sound.py wavbank <banco> <pasta>     todas as amostras de um banco, com a análise
    python tools/sound.py streams [--limit N]         as falas (STREAMS.LUT) e seus marcadores
    python tools/sound.py streamwav <n> <saída.wav>   uma fala (índice da LUT)
    python tools/sound.py music                       as faixas de música (MFXINFO.MXI, SMF)
    python tools/sound.py musicwav <faixa> <saída.wav> [--seconds S]
    python tools/sound.py sfxtable [--elf ACTION.ELF] a tabela SFXOutputData (alcances, duração, laço)
    python tools/sound.py irxsyms <MÓDULO.IRX>        símbolos de um módulo do IOP (endereços do .text)
    python tools/sound.py analyse <arquivo.wav> [--window S]   pico/RMS por janela e cortes (os dumps do jogo)
    python tools/sound.py test                        confere o decodificador com blocos montados à mão

Tudo o que sai daqui (WAV) é dado do jogo: grave em out/.
"""
import argparse
import math
import os
import struct
import sys
import wave

sys.path.insert(0, os.path.dirname(__file__))
import elfsyms  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Coeficientes dos filtros do PS-ADPCM (SPU/SPU2), em 1/64.
FILTERS = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]

SPU_RATE = 48000  # pitch 0x1000 = 48 kHz


def pitch_to_hz(pitch):
    return pitch * SPU_RATE / 4096.0


class Adpcm:
    """Decodificador de PS-ADPCM com o estado de um voz da SPU2 (dois últimos valores)."""

    def __init__(self):
        self.s1 = 0
        self.s2 = 0

    def frame(self, blk):
        """Decodifica um bloco de 16 bytes em 28 amostras de 16 bits. Devolve (amostras, flags)."""
        shift = blk[0] & 0x0F
        filt = (blk[0] >> 4) & 0x0F
        if filt > 4:
            filt = 0  # filtros 5-15 não existem; a SPU usa 0 (não ocorre nos dados)
        if shift > 12:
            shift = 9  # deslocamentos 13-15 se comportam como 9 na SPU
        f0, f1 = FILTERS[filt]
        out = []
        for i in range(28):
            b = blk[2 + i // 2]
            nib = (b >> 4) if (i & 1) else (b & 0x0F)
            s = ((nib << 12) & 0xFFFF)
            if s & 0x8000:
                s -= 0x10000
            s >>= shift
            s += (self.s1 * f0 + self.s2 * f1 + 32) >> 6
            s = max(-0x8000, min(0x7FFF, s))
            self.s2 = self.s1
            self.s1 = s
            out.append(s)
        return out, blk[1]


def decode_adpcm(data, loops=0, stop_at_end=True):
    """Decodifica uma amostra inteira: para no bloco com o bit 0 (fim). Com loops > 0 e o bit 1 (repetir),
    volta ao bloco marcado com o bit 2 (início do laço) loops vezes. Devolve (pcm, info)."""
    dec = Adpcm()
    pcm = []
    nblk = len(data) // 16
    loop_start = None
    end_block = None
    flags_seen = []
    i = 0
    done_loops = 0
    while i < nblk:
        blk = data[i * 16:i * 16 + 16]
        s, flags = dec.frame(blk)
        if flags & 4 and loop_start is None:
            loop_start = i
        if flags:
            flags_seen.append((i, flags))
        pcm.extend(s)
        if flags & 1:
            end_block = i
            if (flags & 2) and done_loops < loops and loop_start is not None:
                done_loops += 1
                i = loop_start
                continue
            if stop_at_end:
                break
        i += 1
    return pcm, {"blocks": nblk, "loop_start": loop_start, "end_block": end_block, "flags": flags_seen}


def analyse(pcm, rate):
    """Prova de que o som é som: duração, pico, RMS, cortes, taxa de cruzamentos por zero e o envelope."""
    n = len(pcm)
    if n == 0:
        return {"n": 0}
    peak = max(abs(x) for x in pcm)
    rms = math.sqrt(sum(x * x for x in pcm) / n)
    clip = sum(1 for x in pcm if x >= 32767 or x <= -32768)
    zc = sum(1 for a, b in zip(pcm, pcm[1:]) if (a < 0) != (b < 0))
    env = []
    w = max(1, n // 10)
    for k in range(0, n, w):
        seg = pcm[k:k + w]
        env.append(math.sqrt(sum(x * x for x in seg) / len(seg)))
    db = lambda v: 20 * math.log10(v / 32768.0) if v > 0 else -120.0  # noqa: E731
    return {
        "n": n, "seconds": n / rate, "peak": peak, "peak_db": db(peak), "rms_db": db(rms), "clip": clip,
        "zcr_hz": zc / (n / rate) / 2.0, "env_db": [round(db(e), 1) for e in env],
    }


def fmt_analysis(a):
    if a.get("n", 0) == 0:
        return "vazio"
    return ("%.2f s pico %d (%.1f dB) rms %.1f dB cortes %d cruzamentos %.0f Hz envelope %s" %
            (a["seconds"], a["peak"], a["peak_db"], a["rms_db"], a["clip"], a["zcr_hz"],
             " ".join("%.0f" % e for e in a["env_db"])))


def write_wav(path, pcm, rate, channels=1):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(int(round(rate)))
        w.writeframes(struct.pack("<%dh" % len(pcm), *pcm))


# ---- bancos de efeitos ----------------------------------------------------------------------------

SFX_PARAMS = struct.Struct("<7i4hiiii5h2bi")  # SFXParameters, 68 bytes (STABS do SFX.IRX)
SFX_PARAM_NAMES = ["ReverbSend", "TrackingType", "InnerRadiusReal", "OuterRadiusReal", "MaxVoices", "Priority",
                   "Group", "MaxReject", "Action2", "IgnoreAge", "Ducker", "DuckerLength", "MasterVolume",
                   "MinDelay", "MaxDelay", "MultiSample", "RandomPick", "Shuffled", "Loop", "Polyphonic",
                   "Outdoors", "PauseInNis", "SampleCount"]
SAMPLE_POOL = struct.Struct("<7i")  # SFXSamplePoolFiles, 28 bytes
SAMPLE_POOL_NAMES = ["FileRef", "PitchOffset", "RandomPitchOffset", "BaseVolume", "RandomVolumeOffset", "Pan",
                     "RandomPan"]
SAMPLE_HEADER = struct.Struct("<9i")  # SampleHeaderData, 36 bytes
SAMPLE_HEADER_NAMES = ["Flags", "Address", "Size", "Frequency", "RealSize", "NumberOfChannels", "BitsPerSample",
                       "psi_SampleHeader", "LoopOffset"]


def lang_dir(root, lang="ENGLISH"):
    return os.path.join(root, "PS2", lang)


def read_sbi(root):
    d = open(os.path.join(root, "PS2", "SBINFO.SBI"), "rb").read()
    return list(struct.unpack("<150i", d))


def bank_slot(root, arg):
    """Aceita o número do arquivo (SB_n) ou, com prefixo h, o hash do banco (SFXLoadSoundBank)."""
    sbi = read_sbi(root)
    if arg.startswith("h"):
        return sbi.index(int(arg[1:], 0))
    return int(arg, 0)


def bank_paths(root, slot, lang="ENGLISH"):
    base = os.path.join(lang_dir(root, lang), "SB_%d" % (slot >> 3), "SB_%d" % slot)
    return base + ".SFX", base + ".SHF", base + ".SBF"


def read_sfx_file(path):
    """SB_n.SFX: u32 n, n x SFXHeader {hash, offset}, depois as definições (SFXParameters + n amostras)."""
    d = open(path, "rb").read()
    n = struct.unpack_from("<i", d, 0)[0]
    out = []
    for i in range(n):
        h, off = struct.unpack_from("<ii", d, 4 + i * 8)
        p = dict(zip(SFX_PARAM_NAMES, SFX_PARAMS.unpack_from(d, off)))
        samples = []
        for k in range(p["SampleCount"]):
            samples.append(dict(zip(SAMPLE_POOL_NAMES, SAMPLE_POOL.unpack_from(d, off + 68 + k * 28))))
        out.append((h, off, p, samples))
    return out


def read_shf_file(path):
    """SB_n.SHF: u32 n, n x SampleHeaderData (36 bytes)."""
    d = open(path, "rb").read()
    n = struct.unpack_from("<i", d, 0)[0]
    return [dict(zip(SAMPLE_HEADER_NAMES, SAMPLE_HEADER.unpack_from(d, 4 + i * 36))) for i in range(n)]


def sample_data(root, slot, idx, lang="ENGLISH"):
    _, shf, sbf = bank_paths(root, slot, lang)
    hdr = read_shf_file(shf)[idx]
    with open(sbf, "rb") as f:
        f.seek(hdr["Address"])
        return hdr, f.read(hdr["Size"])


LEVEL_BANKS = {  # Sound_Ready (0x001dceb8): o banco (hash) que cada fase carrega
    0x7000001: 1, 0x7000002: 2, 0x7000003: 3, 0x7000004: 8, 0x7000005: 5, 0x7000006: 4, 0x7000007: 6,
    0x7000008: 7, 0x7000009: 0x17, 0x700000a: 0x18, 0x700000b: 0x19, 0x700000c: 0x1a, 0x700000d: 0x37,
    0x7000011: 0x1e, 0x7000012: 0x1f, 0x7000013: 0x20, 0x7000014: 0x27, 0x7000015: 0x28, 0x7000016: 0x2b,
    0x700001a: 0x25, 0x700001b: 0x25, 0x7000041: 0xb, 0x700004a: 0x21,
    0x7000048: "0xc / 0x38 (alterna)", 0x7000021: "0x16 (multijogador: 21-29, 4b, 4c)",
}


def cmd_banks(args):
    sbi = read_sbi(args.root)
    by_hash = {}
    for lvl, b in LEVEL_BANKS.items():
        by_hash.setdefault(b, []).append("%08x" % lvl)
    for slot, h in enumerate(sbi):
        if h < 0:
            continue
        sfx, shf, sbf = bank_paths(args.root, slot)
        defs = read_sfx_file(sfx)
        hdrs = read_shf_file(shf)
        size = os.path.getsize(sbf)
        streamed = sum(1 for _, _, p, s in defs for x in s if x["FileRef"] < 0)
        lv = ",".join(by_hash.get(h, []))
        print("SB_%-2d hash 0x%02x  sons %4d  amostras %4d  falas %4d  SBF %8d bytes  fases %s" %
              (slot, h, len(defs), len(hdrs), streamed, size, lv))


def cmd_sfx(args):
    slot = bank_slot(args.root, args.bank)
    sfx, _, _ = bank_paths(args.root, slot)
    for h, off, p, samples in read_sfx_file(sfx):
        print("som 0x%03x (%d) @0x%x: %s" % (h, h, off, " ".join("%s=%d" % (k, v) for k, v in p.items())))
        for s in samples:
            print("    " + " ".join("%s=%d" % (k, v) for k, v in s.items()))


def cmd_samples(args):
    slot = bank_slot(args.root, args.bank)
    _, shf, _ = bank_paths(args.root, slot)
    for i, h in enumerate(read_shf_file(shf)):
        print("%4d %s  (%.0f Hz)" % (i, " ".join("%s=0x%x" % (k, v) for k, v in h.items()),
                                     pitch_to_hz(h["Frequency"])))


def cmd_wav(args):
    slot = bank_slot(args.root, args.bank)
    hdr, data = sample_data(args.root, slot, args.sample)
    pcm, info = decode_adpcm(data, loops=args.loops)
    rate = pitch_to_hz(hdr["Frequency"])
    write_wav(args.out, pcm, rate)
    print("%s: %d blocos, laço %s, fim %s, %.0f Hz" % (args.out, info["blocks"], info["loop_start"],
                                                       info["end_block"], rate))
    print("  " + fmt_analysis(analyse(pcm, rate)))


def cmd_wavbank(args):
    slot = bank_slot(args.root, args.bank)
    _, shf, _ = bank_paths(args.root, slot)
    for i, _h in enumerate(read_shf_file(shf)):
        hdr, data = sample_data(args.root, slot, i)
        pcm, info = decode_adpcm(data)
        rate = pitch_to_hz(hdr["Frequency"])
        path = os.path.join(args.dir, "SB_%d_%03d.wav" % (slot, i))
        write_wav(path, pcm, rate)
        loop = "laço@%s" % info["loop_start"] if hdr["Flags"] & 1 else "      "
        print("%3d %6.0f Hz %s %s" % (i, rate, loop, fmt_analysis(analyse(pcm, rate))))


# ---- falas em streaming -------------------------------------------------------------------------

MARKER_HEADER = struct.Struct("<5i")    # StreamMarkerHeaderData
MARKER = struct.Struct("<8i")           # MusicMarkerData (32 bytes)
MARKER_NAMES = ["Name", "Pos", "MType", "Flags", "Extra", "LoopStart", "MarkerCount", "LoopMarkerCount"]


def read_markers(d, base=0):
    """Cabeçalho de marcadores (de uma fala ou de um .SMF de música)."""
    sc, mc, so, mo, vol = MARKER_HEADER.unpack_from(d, base)
    starts = []
    for i in range(sc):
        o = base + so + i * 52  # MusicMarkerStartData: MusicMarkerData + MarkerPos, IsInstant, InstantBuffer, State[2]
        m = dict(zip(MARKER_NAMES, MARKER.unpack_from(d, o)))
        m["MarkerPos"], m["IsInstant"], m["InstantBuffer"] = struct.unpack_from("<3i", d, o + 32)
        starts.append(m)
    marks = [dict(zip(MARKER_NAMES, MARKER.unpack_from(d, base + mo + i * 32))) for i in range(mc)]
    return {"StartMarkerCount": sc, "MarkerCount": mc, "BaseVolume": vol, "starts": starts, "markers": marks}


def read_lut(root, lang="ENGLISH"):
    d = open(os.path.join(lang_dir(root, lang), "STREAMS", "STREAMS.LUT"), "rb").read()
    return [struct.unpack_from("<4I", d, i * 16) for i in range(len(d) // 16)]


def stream_data(root, idx, lang="ENGLISH"):
    lut = read_lut(root, lang)
    moff, msize, doff, dsize = lut[idx]
    with open(os.path.join(lang_dir(root, lang), "STREAMS", "STREAMS.BIN"), "rb") as f:
        f.seek(moff)
        mk = f.read(msize)
        f.seek(doff)
        data = f.read(dsize)
    return read_markers(mk), data


STREAM_PITCH = 0x759  # StartSample: FileRef < 0 toca a fala com este pitch (22039 Hz)


def cmd_streams(args):
    lut = read_lut(args.root)
    print("%d falas" % len(lut))
    for i, (moff, msize, doff, dsize) in enumerate(lut[:args.limit]):
        mk, _ = stream_data(args.root, i)
        ends = [m["Pos"] for m in mk["markers"] if m["MType"] == 9]
        print("%4d marcadores 0x%08x+%d dados 0x%08x+%d (%.2f s) volume %d fim %s" %
              (i, moff, msize, doff, dsize, dsize / 16 * 28 / pitch_to_hz(STREAM_PITCH), mk["BaseVolume"], ends))


def cmd_streamwav(args):
    mk, data = stream_data(args.root, args.n)
    pcm, info = decode_adpcm(data, stop_at_end=False)
    rate = pitch_to_hz(STREAM_PITCH)
    write_wav(args.out, pcm, rate)
    print("%s: %d blocos, flags %s" % (args.out, info["blocks"], info["flags"][:8]))
    print("  " + fmt_analysis(analyse(pcm, rate)))


# ---- música ---------------------------------------------------------------------------------------

def read_mxi(root):
    d = open(os.path.join(root, "PS2", "MFXINFO.MXI"), "rb").read()
    return list(struct.unpack("<150i", d))


def music_paths(root, n):
    base = os.path.join(root, "PS2", "MUSIC", "MFX_%d" % (n >> 4), "MFX_%d" % n)
    return base + ".SMF", base + ".SSD"


MUSIC_INTERLEAVE = 0x80  # bytes de cada canal por vez no .SSD: 0x80 esquerdo, 0x80 direito (psiStreamMemCpy)


def cmd_music(args):
    mxi = read_mxi(args.root)
    for n in range(len(mxi)):
        smf, ssd = music_paths(args.root, n)
        if not os.path.exists(smf):
            continue
        mk = read_markers(open(smf, "rb").read())
        size = os.path.getsize(ssd)
        print("MFX_%-2d hash 0x%02x  %d bytes (%.1f s estéreo)  marcadores iniciais %d marcadores %d volume %d" %
              (n, mxi[n - 1] if n - 1 < len(mxi) else -1, size, size / 2 / 16 * 28 / 32000.0,
               mk["StartMarkerCount"], mk["MarkerCount"], mk["BaseVolume"]))
        if args.verbose:
            for m in mk["starts"]:
                print("    início %s" % m)
            for m in mk["markers"]:
                print("    %s" % m)


def cmd_musicwav(args):
    _, ssd = music_paths(args.root, args.track)
    d = open(ssd, "rb").read()
    il = args.interleave
    nbytes = min(len(d), int(args.seconds * 32000 / 28 * 16 * 2)) if args.seconds else len(d)
    left, right = Adpcm(), Adpcm()
    pcm_l, pcm_r = [], []
    for off in range(0, nbytes - 2 * il + 1, 2 * il):
        for k in range(0, il, 16):
            pcm_l.extend(left.frame(d[off + k:off + k + 16])[0])
            pcm_r.extend(right.frame(d[off + il + k:off + il + k + 16])[0])
    inter = [v for pair in zip(pcm_l, pcm_r) for v in pair]
    write_wav(args.out, inter, args.rate, channels=2)
    print("%s: %.1f s" % (args.out, len(pcm_l) / args.rate))
    print("  esquerdo " + fmt_analysis(analyse(pcm_l, args.rate)))
    print("  direito  " + fmt_analysis(analyse(pcm_r, args.rate)))
    num = sum(a * b for a, b in zip(pcm_l, pcm_r))
    den = math.sqrt(sum(a * a for a in pcm_l) * sum(b * b for b in pcm_r)) or 1.0
    print("  correlação esquerdo/direito %.3f (canais de verdade: alta; intercalação errada: perto de 0)" % (num / den))


# ---- a tabela do ELF ------------------------------------------------------------------------------

def cmd_sfxtable(args):
    elf = args.elf or os.path.join(args.root, "ACTION.ELF")
    d, sections, syms = elfsyms.load(elf)
    by_name = {name: (value, size) for value, size, _t, _s, name in syms}
    addr, size = by_name["SFXOutputData"]

    def off_of(vaddr):
        for _n, s in sections:
            if s[3] and s[3] <= vaddr < s[3] + s[5] and s[1] != 8:
                return vaddr - s[3] + s[4]
        raise ValueError(hex(vaddr))

    base = off_of(addr)
    n = size // 0x18
    print("SFXOutputData 0x%08x: %d sons x 0x18 bytes" % (addr, n))
    print("  som   +0     min    max   alerta  duração laço 3d")
    for i in range(n):
        w0, mn, mx, al, dur, loop, cull = struct.unpack_from("<i4fBB", d, base + i * 0x18)
        if args.all or loop or cull or dur:
            print("  %4d %4d %6.1f %6.1f %7.2f %7.2f %4d %2d" % (i, w0, mn, mx, al, dur, loop, cull))


def cmd_irxsyms(args):
    path = args.irx if os.path.exists(args.irx) else os.path.join(args.root, "MODULES", args.irx)
    _d, sections, syms = elfsyms.load(path)
    names = [n for n, _s in sections]
    for value, size, typ, shndx, name in sorted(syms):
        if name and typ in ("FUNC", "OBJECT", "NOTYPE") and shndx < len(names):
            print("%08x %6d %-6s %-8s %s" % (value, size, typ, names[shndx], name))


# ---- análise de um WAV (os dumps de nightfire.exe --audio-dump) ----

def cmd_analyse(args):
    with wave.open(args.wav, "rb") as w:
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        data = w.readframes(n)
    pcm = struct.unpack("<%dh" % (len(data) // 2), data)
    win = max(1, int(args.window * rate))
    db = lambda v: 20 * math.log10(v / 32768.0) if v > 0 else -120.0  # noqa: E731
    total_clip = sum(1 for x in pcm if x >= 32767 or x <= -32768)
    print("%s: %d canais, %d Hz, %.2f s, cortes %d" % (args.wav, ch, rate, n / rate, total_clip))
    loud = 0
    for start in range(0, n, win):
        seg = pcm[start * ch:(start + win) * ch]
        if not seg:
            break
        l = seg[0::ch]
        r = seg[1::ch] if ch > 1 else l
        peak = max(max(abs(x) for x in l), max(abs(x) for x in r))
        rms_l = math.sqrt(sum(x * x for x in l) / len(l))
        rms_r = math.sqrt(sum(x * x for x in r) / len(r))
        zc = sum(1 for a, b in zip(l, l[1:]) if (a < 0) != (b < 0)) / (len(l) / rate) / 2.0
        mark = "#" * max(0, int((db(max(rms_l, rms_r)) + 60) / 3))
        if db(max(rms_l, rms_r)) > -60:
            loud += 1
        print("%7.2f s  pico %6.1f dB  rms E %6.1f D %6.1f dB  cruz %5.0f Hz  %s" %
              (start / rate, db(peak), db(rms_l), db(rms_r), zc, mark))
    print("janelas com som (rms > -60 dB): %d" % loud)


# ---- teste do decodificador -----------------------------------------------------------------------

def cmd_test(_args):
    # Bloco com filtro 0, deslocamento 12: cada nibble n vale n (com sinal de 4 bits).
    blk = bytes([0x0C, 0x00] + [0x10 | 0x0F] + [0x00] * 13)  # 0xF -> -1, 0x1 -> 1, depois zeros
    s, _ = Adpcm().frame(blk)
    assert s[:3] == [-1, 1, 0], s[:3]
    # Deslocamento 0: o nibble vai para os 4 bits de cima (n << 12).
    s, _ = Adpcm().frame(bytes([0x00, 0x00, 0x07] + [0] * 13))
    assert s[0] == 0x7000, s[0]
    # Filtro 1 (60/64): sem entrada nova, o valor decai pela regra (s1 * 60 + 32) >> 6.
    dec = Adpcm()
    dec.s1 = 1000
    s, _ = dec.frame(bytes([0x1C, 0x00] + [0] * 14))
    assert s[0] == (1000 * 60 + 32) >> 6 and s[1] == (s[0] * 60 + 32) >> 6, s[:2]
    # Filtro 2 com os dois valores anteriores e o corte em 16 bits.
    dec = Adpcm()
    dec.s1, dec.s2 = 30000, -30000
    s, _ = dec.frame(bytes([0x20, 0x00, 0x07] + [0] * 13))
    assert s[0] == 32767, s[0]
    # Laço: bloco 1 marca o início (4), bloco 2 fim + repetir (3).
    data = bytes([0x0C, 0x00] + [0x11] * 14) + bytes([0x0C, 0x04] + [0x22] * 14) + bytes([0x0C, 0x03] + [0x33] * 14)
    pcm, info = decode_adpcm(data, loops=2)
    assert info["loop_start"] == 1 and info["end_block"] == 2 and len(pcm) == 28 * 7, (info, len(pcm))
    print("ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=ROOT, help="pasta com os dados do disco (PS2/, ACTION.ELF, MODULES/)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("banks")
    p = sub.add_parser("sfx"); p.add_argument("bank")  # noqa: E702
    p = sub.add_parser("samples"); p.add_argument("bank")  # noqa: E702
    p = sub.add_parser("wav"); p.add_argument("bank"); p.add_argument("sample", type=int)  # noqa: E702
    p.add_argument("out"); p.add_argument("--loops", type=int, default=0)  # noqa: E702
    p = sub.add_parser("wavbank"); p.add_argument("bank"); p.add_argument("dir")  # noqa: E702
    p = sub.add_parser("streams"); p.add_argument("--limit", type=int, default=40)  # noqa: E702
    p = sub.add_parser("streamwav"); p.add_argument("n", type=int); p.add_argument("out")  # noqa: E702
    p = sub.add_parser("music"); p.add_argument("-v", "--verbose", action="store_true")  # noqa: E702
    p = sub.add_parser("musicwav"); p.add_argument("track", type=int); p.add_argument("out")  # noqa: E702
    p.add_argument("--seconds", type=float, default=30.0); p.add_argument("--rate", type=float, default=32000)  # noqa: E702
    p.add_argument("--interleave", type=lambda s: int(s, 0), default=MUSIC_INTERLEAVE)
    p = sub.add_parser("sfxtable"); p.add_argument("--elf"); p.add_argument("--all", action="store_true")  # noqa: E702
    p = sub.add_parser("irxsyms"); p.add_argument("irx")  # noqa: E702
    p = sub.add_parser("analyse"); p.add_argument("wav"); p.add_argument("--window", type=float, default=1.0)  # noqa: E702
    sub.add_parser("test")
    args = ap.parse_args()
    globals()["cmd_" + args.cmd](args)


if __name__ == "__main__":
    main()
