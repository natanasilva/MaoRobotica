import math


class Smoother:
    def __init__(self, seconds):
        self.seconds = seconds
        self.values = None
        self.time = None

    def reset(self):
        self.values = self.time = None

    def update(self, values, now):
        if self.values is None:
            self.values = list(values)
        else:
            alpha = 1 - math.exp(-max(0, now - self.time) / self.seconds)
            self.values = [old + alpha * (new - old) for old, new in zip(self.values, values)]
        self.time = now
        return list(self.values)
