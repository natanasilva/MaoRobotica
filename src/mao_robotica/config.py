import json
import math
from pathlib import Path

from . import FINGERS

DEFAULTS = {"open_hold_seconds": 0.8, "open_max_bend": 0.25, "open_min_facing": 0.6}


def load_config(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    # Chaves novas têm padrão, para que perfis antigos continuem válidos.
    for key, value in DEFAULTS.items():
        data.setdefault(key, value)
    if data["hand"] not in ("Left", "Right", "Any"):
        raise ValueError("hand deve ser Left, Right ou Any")
    if type(data["camera"]) is not int or data["camera"] < 0:
        raise ValueError("camera deve ser um índice não negativo")
    for key, low, high in (("send_hz", 5, 50), ("smoothing_seconds", 0.01, 1),
                           ("lost_seconds", 0.05, 0.4), ("min_hand_score", 0.5, 1),
                           ("open_hold_seconds", 0.3, 3), ("open_max_bend", 0.05, 0.4),
                           ("open_min_facing", 0, 1)):
        value = data[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
            raise ValueError(f"{key} deve estar entre {low} e {high}")
    if not isinstance(data["model"], str) or not data["model"]:
        raise ValueError("model deve conter o caminho do modelo")
    for finger in FINGERS:
        entry = data["calibration"][finger]
        opened, closed = entry["open"], entry["closed"]
        if any(isinstance(v, bool) or not isinstance(v, (int, float)) for v in (opened, closed)):
            raise ValueError(f"Calibração de {finger} deve conter números")
        if not all(math.isfinite(v) and 0 <= v <= 1 for v in (opened, closed)) or closed - opened < 0.08:
            raise ValueError(f"Calibração inválida para {finger}: fechamento deve superar abertura em pelo menos 0.08")
    return data


def save_config(path, data):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)
