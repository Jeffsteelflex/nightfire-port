"""Grava o estado dos corpos rígidos das missões de carro a cada quadro, lendo a RAM do PCSX2 pelo PINE.

O PINE é a interface de IPC do PCSX2 (Ferramentas > Configurações > Avançado > "Enable PINE", porta 28011 no
Windows; no Linux o soquete /tmp/pcsx2.sock). Este script só usa a biblioteca padrão e não muda nada na emulação: lê
a Simulation do DRIVING.ELF americano (SLUS-20579) enquanto uma missão de carro roda e grava um TSV por quadro da
simulação, para comparar com o porte (docs/driving.md, seção 11, "A verdade do PCSX2").

    python tools/pcsx2_pine.py status                      o PCSX2 responde? o DRIVING.ELF está carregado?
    python tools/pcsx2_pine.py record out/driving/paris.tsv --frames 600
    python tools/pcsx2_pine.py test                        o protocolo contra um servidor PINE falso (sem PCSX2)

O que é lido (endereços do DRIVING.ELF do disco; docs/driving.md seção 11):
  0x339190 a Simulation; +0x1c o vetor dos RigidBody (48 x 0x80 bytes), +0x24 o dos RigidBodyInfo (48 x 0x500),
           +0x270 os donos (não nulo: vaga em uso), +0xdb4 o estado (3: parada), +0xdc0 o passo (1/60)
  0x355e60 o contador de quadros da simulação (ESimFrameUpdate o soma antes de copiar os corpos de volta do
           scratchpad para a RAM principal; o vetor de +0x1c tem o estado do fim do quadro)
  0x396338 a cadeia "Physics:Rigid" (confere que é o DRIVING.ELF que está na RAM)
De cada corpo em uso: o RigidBody inteiro (quatérnio, posição, velocidade, velocidade angular, momento, momento
angular, força e torque, que são os que o passo do quadro seguinte integra, os bytes de estado) e, do RigidBodyInfo,
a orientação e o tensor inverso no mundo. Os floats saem em hexadecimal (os bits) e em decimal.

O PINE não para a emulação: o script espera o contador de quadros mudar, lê, e lê o contador de novo; se mudou no
meio, a leitura é descartada e repetida (a gravação marca os quadros perdidos).
"""
import socket
import struct
import sys
import threading
import time

PORT = 28011
SIM = 0x339190
FRAMES = 0x355E60
SIGNATURE = 0x396338
SIGNATURE_TEXT = b"Physics:Rigid"
SLOTS = 48

OP_READ8, OP_READ32, OP_READ64 = 0, 2, 3
OP_TITLE, OP_STATUS = 0x0B, 0x0F


class Pine:
    def __init__(self, host="127.0.0.1", port=PORT, timeout=5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)

    def close(self):
        self.sock.close()

    def _recv(self, n):
        data = b""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("o PCSX2 fechou a conexão")
            data += chunk
        return data

    def batch(self, commands):
        """commands: lista de (opcode, args bytes, tamanho da resposta). Devolve as respostas (bytes) na ordem."""
        body = b"".join(bytes([op]) + args for op, args, _ in commands)
        self.sock.sendall(struct.pack("<I", len(body) + 4) + body)
        size = struct.unpack("<I", self._recv(4))[0]
        payload = self._recv(size - 4)
        if not payload or payload[0] != 0:
            raise RuntimeError("o PINE recusou o pedido")
        out, pos = [], 1
        for _, _, n in commands:
            out.append(payload[pos:pos + n])
            pos += n
        return out

    def read32(self, addr):
        return struct.unpack("<I", self.batch([(OP_READ32, struct.pack("<I", addr), 4)])[0])[0]

    def read_block(self, addr, size, chunk=4096):
        """size bytes a partir de addr (múltiplo de 8), em leituras de 64 bits agrupadas."""
        out = b""
        for base in range(addr, addr + size, chunk):
            n = min(chunk, addr + size - base)
            cmds = [(OP_READ64, struct.pack("<I", a), 8) for a in range(base, base + n, 8)]
            out += b"".join(self.batch(cmds))
        return out

    def status(self):
        return struct.unpack("<I", self.batch([(OP_STATUS, b"", 4)])[0])[0]

    def title(self):
        data = self.batch([(OP_TITLE, b"", 0)])
        return data


BODY_FIELDS = [("qx", 0x00), ("qy", 0x04), ("qz", 0x08), ("qw", 0x0C), ("px", 0x10), ("py", 0x14), ("pz", 0x18),
               ("iix", 0x1C), ("vx", 0x20), ("vy", 0x24), ("vz", 0x28), ("iiy", 0x2C), ("wx", 0x30), ("wy", 0x34),
               ("wz", 0x38), ("iiz", 0x3C), ("Px", 0x40), ("Py", 0x44), ("Pz", 0x48), ("m", 0x4C), ("Lx", 0x50),
               ("Ly", 0x54), ("Lz", 0x58), ("Fx", 0x60), ("Fy", 0x64), ("Fz", 0x68), ("Tx", 0x70), ("Ty", 0x74),
               ("Tz", 0x78), ("raio", 0x7C)]


def is_driving(p):
    data = p.read_block(SIGNATURE, 16)
    return data.startswith(SIGNATURE_TEXT)


def header():
    cols = ["quadro", "vaga", "tipo", "estado", "alavancas_chao"]
    for name, _ in BODY_FIELDS:
        cols += [name, name + "_bits"]
    for i in range(3):
        for j in range(3):
            cols.append(f"R{i}{j}")
    for i in range(3):
        for j in range(3):
            cols.append(f"Iw{i}{j}")
    return "\t".join(cols)


def rows_for_frame(frame, owners, bodies, infos):
    rows = []
    for slot in range(SLOTS):
        if owners[slot] == 0:
            continue
        b = bodies[slot * 0x80:(slot + 1) * 0x80]
        info = infos[slot]
        ground, typ, state = struct.unpack_from("<bbb", b, 0x6C)
        cols = [str(frame), str(slot), str(typ), str(state), str(ground)]
        for _, off in BODY_FIELDS:
            bits = struct.unpack_from("<I", b, off)[0]
            cols += [repr(struct.unpack_from("<f", b, off)[0]), f"{bits:08x}"]
        for i in range(3):
            for j in range(3):
                cols.append(repr(struct.unpack_from("<f", info, 0x00 + 16 * i + 4 * j)[0]))
        for i in range(3):
            for j in range(3):
                cols.append(repr(struct.unpack_from("<f", info, 0x40 + 16 * i + 4 * j)[0]))
        rows.append("\t".join(cols))
    return rows


def snapshot(p):
    bodies_ptr = p.read32(SIM + 0x1C)
    infos_ptr = p.read32(SIM + 0x24)
    owners = struct.unpack(f"<{SLOTS}I", p.read_block(SIM + 0x270, 4 * SLOTS))
    bodies = p.read_block(bodies_ptr, 0x80 * SLOTS)
    infos = {}
    for slot in range(SLOTS):
        if owners[slot]:
            infos[slot] = p.read_block(infos_ptr + 0x500 * slot, 0x80)
    return owners, bodies, infos


def cmd_status(args):
    p = Pine()
    st = p.status()
    print(f"PCSX2 responde: estado {st} (0 rodando, 1 pausado, 2 desligado)")
    print("DRIVING.ELF na RAM:", "sim" if is_driving(p) else "não (só durante uma missão de carro)")
    if is_driving(p):
        print(f"quadros da simulação: {p.read32(FRAMES)}; corpos em uso: "
              f"{sum(1 for o in struct.unpack('<48I', p.read_block(SIM + 0x270, 192)) if o)}")
    p.close()
    return 0


def cmd_record(args):
    if not args:
        print(__doc__)
        return 1
    out_path = args[0]
    frames = 600
    if "--frames" in args:
        frames = int(args[args.index("--frames") + 1])
    p = Pine()
    if not is_driving(p):
        print("o DRIVING.ELF não está na RAM: comece uma missão de carro antes")
        return 1
    last = p.read32(FRAMES)
    lost = 0
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(header() + "\n")
        done = 0
        while done < frames:
            cur = p.read32(FRAMES)
            if cur == last:
                time.sleep(0.001)
                continue
            if cur != last + 1:
                lost += cur - last - 1
            owners, bodies, infos = snapshot(p)
            if p.read32(FRAMES) != cur:  # a emulação andou durante a leitura: descarta
                lost += 1
                last = cur
                continue
            for row in rows_for_frame(cur, owners, bodies, infos):
                f.write(row + "\n")
            last = cur
            done += 1
    print(f"{done} quadros em {out_path}; {lost} perdidos (a leitura não acompanhou a emulação)")
    p.close()
    return 0


def fake_server(memory, ready):
    """Um PINE falso: responde Read8/32/64 e Status a partir de um dicionário de memória (teste)."""
    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    ready.append(srv.getsockname()[1])
    conn, _ = srv.accept()
    with conn:
        while True:
            head = conn.recv(4)
            if not head:
                break
            size = struct.unpack("<I", head)[0]
            body = b""
            while len(body) < size - 4:
                body += conn.recv(size - 4 - len(body))
            out, pos = b"", 0
            while pos < len(body):
                op = body[pos]
                pos += 1
                if op in (OP_READ8, OP_READ32, OP_READ64):
                    addr = struct.unpack_from("<I", body, pos)[0]
                    pos += 4
                    n = {OP_READ8: 1, OP_READ32: 4, OP_READ64: 8}[op]
                    out += bytes(memory.get(addr + i, 0) for i in range(n))
                elif op == OP_STATUS:
                    out += struct.pack("<I", 0)
            reply = b"\x00" + out
            conn.sendall(struct.pack("<I", len(reply) + 4) + reply)
    srv.close()


def cmd_test(args):
    memory = {}

    def put(addr, data):
        for i, byte in enumerate(data):
            memory[addr + i] = byte

    put(SIGNATURE, SIGNATURE_TEXT + b"\x00")
    put(SIM + 0x1C, struct.pack("<I", 0x01000000))
    put(SIM + 0x24, struct.pack("<I", 0x01010000))
    put(SIM + 0x270 + 4 * 3, struct.pack("<I", 0x00500000))
    body = bytearray(0x80)
    struct.pack_into("<4f", body, 0x00, 0.0, 0.0, 0.0, 1.0)
    struct.pack_into("<3f", body, 0x10, 1.5, 2.5, -3.5)
    struct.pack_into("<f", body, 0x4C, 1660.0)
    struct.pack_into("<bbbb", body, 0x6C, 4, 1, 2, 3)
    put(0x01000000 + 3 * 0x80, bytes(body))
    put(0x01010000 + 3 * 0x500, struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1))
    ready = []
    t = threading.Thread(target=fake_server, args=(memory, ready), daemon=True)
    t.start()
    while not ready:
        time.sleep(0.01)
    p = Pine(port=ready[0])
    ok = p.status() == 0 and is_driving(p)
    owners, bodies, infos = snapshot(p)
    rows = rows_for_frame(7, owners, bodies, infos)
    ok &= len(rows) == 1
    cols = rows[0].split("\t")
    names = header().split("\t")
    ok &= cols[names.index("vaga")] == "3" and cols[names.index("tipo")] == "1"
    ok &= cols[names.index("px")] == "1.5" and cols[names.index("m")] == "1660.0"
    ok &= cols[names.index("R11")] == "1.0"
    p.close()
    print("ok" if ok else "FALHOU", "(protocolo PINE contra um servidor falso)")
    return 0 if ok else 1


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd = {"status": cmd_status, "record": cmd_record, "test": cmd_test}.get(argv[1])
    if not cmd:
        print(__doc__)
        return 1
    return cmd(argv[2:])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
