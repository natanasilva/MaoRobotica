"""Geometria em coordenadas 3D no mesmo sistema de unidades."""
import math

from . import FINGERS


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def bend(a, b, c):
    """0 = articulação reta; 1 = dobra de 180 graus."""
    u = [x - y for x, y in zip(a, b)]
    v = [x - y for x, y in zip(c, b)]
    denominator = math.sqrt(sum(x*x for x in u) * sum(x*x for x in v))
    if denominator < 1e-10:
        raise ValueError("Articulação degenerada")
    cosine = max(-1.0, min(1.0, sum(x*y for x, y in zip(u, v)) / denominator))
    return 1.0 - math.acos(cosine) / math.pi


def raw_flexions(points):
    if len(points) != 21 or any(len(p) != 3 or not all(math.isfinite(v) for v in p) for p in points):
        raise ValueError("São necessários 21 pontos 3D finitos")
    palm_width = distance(points[5], points[17])
    if palm_width < 1e-8:
        raise ValueError("Palma degenerada")
    thumb_bend = (bend(points[1], points[2], points[3]) + bend(points[2], points[3], points[4])) / 2
    # Proximidade à base do mínimo complementa a flexão do polegar.
    opposition = max(0.0, min(1.0, 1.0 - distance(points[4], points[17]) / (2 * palm_width)))
    values = [0.65 * thumb_bend + 0.35 * opposition]
    for base in (5, 9, 13, 17):
        values.append(0.4 * bend(points[0], points[base], points[base+1])
                      + 0.4 * bend(points[base], points[base+1], points[base+2])
                      + 0.2 * bend(points[base+1], points[base+2], points[base+3]))
    return values


def normalize(values, calibration):
    return [max(0.0, min(1.0, (v - calibration[f]["open"]) /
                            (calibration[f]["closed"] - calibration[f]["open"])))
            for f, v in zip(FINGERS, values)]
