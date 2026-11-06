"""Protocolo ASCII v1: checksum XOR dos bytes anteriores ao asterisco."""
import math


def encode(payload):
    raw = payload.encode("ascii")
    if len(raw) > 80 or any(c in raw for c in (b"*", b"\n", b"\r")):
        raise ValueError("Payload inválido")
    checksum = 0
    for value in raw:
        checksum ^= value
    return raw + f"*{checksum:02X}\n".encode("ascii")


def decode(frame):
    payload, check = frame.rstrip(b"\r\n").rsplit(b"*", 1)
    if len(check) != 2 or encode(payload.decode("ascii")).rstrip(b"\n") != payload + b"*" + check.upper():
        raise ValueError("Checksum inválido")
    return payload.decode("ascii")


def set_frame(sequence, values):
    if len(values) != 5 or any(not math.isfinite(v) or not 0 <= v <= 1 for v in values):
        raise ValueError("Informe cinco flexões entre 0 e 1")
    if not 0 <= sequence <= 65535:
        raise ValueError("Sequência fora da faixa")
    return encode(f"SET,{sequence}," + ",".join(str(round(v * 1000)) for v in values))
