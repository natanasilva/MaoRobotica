"""Captura automática da referência de mão aberta."""
from collections import deque
import statistics


class OpenPalmLock:
    """Acumula leituras de mão espalmada e devolve a referência quando ficam estáveis."""

    def __init__(self, hold_seconds=0.8, tolerance=0.08, min_samples=8):
        self.hold_seconds = hold_seconds
        self.tolerance = tolerance
        self.min_samples = min_samples
        self.samples = deque()

    def reset(self):
        self.samples.clear()

    def progress(self, now):
        if not self.samples:
            return 0.0
        return min(1.0, (now - self.samples[0][0]) / self.hold_seconds)

    def update(self, now, raw, open_palm):
        if not open_palm:
            self.samples.clear()
            return None
        self.samples.append((now, list(raw)))
        recent = [v for _, v in self.samples]
        if any(max(v[i] for v in recent) - min(v[i] for v in recent) > self.tolerance
               for i in range(len(raw))):
            # Movimento: reinicia a contagem a partir desta leitura.
            self.samples = deque([self.samples[-1]])
            return None
        if now - self.samples[0][0] < self.hold_seconds or len(self.samples) < self.min_samples:
            return None
        reference = [statistics.median(v[i] for v in recent) for i in range(len(raw))]
        self.samples.clear()
        return reference
