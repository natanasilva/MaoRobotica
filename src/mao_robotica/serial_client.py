import time

from .protocol import decode, encode, set_frame


class SerialClient:
    def __init__(self, port):
        import serial
        self.serial = serial.Serial(port, 115200, timeout=0, write_timeout=0.15)
        self.buffer = bytearray()
        self.sequence = 0
        self.armed = False
        self.last_ack = 0.0
        self.pending = []
        try:
            # Uno/Nano podem reiniciar ao abrir a porta. HELLO também atende placas já ligadas.
            self._exchange("HELLO", "READY,1", timeout=4.0)
        except Exception:
            self.serial.close()
            raise

    def _write(self, frame):
        if self.serial.write(frame) != len(frame):
            raise RuntimeError("Escrita serial incompleta")

    def poll(self):
        chunk = self.serial.read(min(self.serial.in_waiting, 1024))
        self.buffer.extend(chunk)
        messages = []
        while b"\n" in self.buffer:
            line, _, rest = self.buffer.partition(b"\n")
            self.buffer = bytearray(rest)
            try:
                message = decode(line)
            except (ValueError, UnicodeError):
                continue
            messages.append(message)
            if message.startswith("ACK,"):
                try:
                    sequence = int(message.split(",", 1)[1])
                    index = self.pending.index(sequence)
                except ValueError:
                    pass
                else:
                    del self.pending[:index + 1]
                    self.last_ack = time.monotonic()
            if message in ("DISARMED", "FAULT,TIMEOUT", "FAULT,I2C") or message.startswith("READY,"):
                self.armed = False
            if message.startswith("ERR,") or message.startswith("FAULT,"):
                self.armed = False
                raise RuntimeError(f"Arduino: {message}")
        if len(self.buffer) > 256:
            self.buffer.clear()
        return messages

    def _exchange(self, command, expected, timeout=1.0):
        deadline = time.monotonic() + timeout
        next_send = 0.0
        while time.monotonic() < deadline:
            if time.monotonic() >= next_send:
                self._write(encode(command))
                next_send = time.monotonic() + 0.3
            if expected in self.poll():
                return
            time.sleep(0.01)
        raise RuntimeError(f"Arduino não respondeu a {command}")

    def arm(self):
        self._exchange("ARM", "ARMED")
        self.armed = True
        self.pending.clear()
        self.last_ack = time.monotonic()

    def stop(self):
        self.armed = False
        self.pending.clear()
        self._write(encode("STOP"))

    def send(self, values):
        self.poll()
        if not self.armed:
            return
        if time.monotonic() - self.last_ack > 0.5:
            self.stop()
            raise RuntimeError("Sem confirmação de comandos pelo Arduino")
        self._write(set_frame(self.sequence, values))
        self.pending.append(self.sequence)
        self.sequence = (self.sequence + 1) % 65536

    def close(self):
        try:
            self.stop()
        finally:
            self.serial.close()
