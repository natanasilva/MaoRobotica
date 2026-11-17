import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from mao_robotica.config import load_config, save_config
from mao_robotica.finger_mapping import (bend, is_open_palm, normalize, normalize_from_open,
                                         palm_facing, raw_flexions)
from mao_robotica.filters import Smoother
from mao_robotica.reference import OpenPalmLock
from mao_robotica.protocol import decode, encode, set_frame

ROOT = Path(__file__).resolve().parents[1]


class ProtocolTests(unittest.TestCase):
    def test_round_trip(self):
        frame = set_frame(65535, [0, 0.25, 0.5, 0.75, 1])
        self.assertEqual(decode(frame), "SET,65535,0,250,500,750,1000")

    def test_corruption(self):
        frame = encode("STOP")
        with self.assertRaises(ValueError):
            decode(frame.replace(b"STOP", b"STOQ"))

    def test_invalid_values(self):
        for values in ([0]*4, [0]*4+[float("nan")], [0]*4+[1.01], [0]*4+[-0.1]):
            with self.assertRaises(ValueError):
                set_frame(1, values)

    def test_injected_delimiter(self):
        with self.assertRaises(ValueError):
            encode("STOP\nARM")


class GeometryTests(unittest.TestCase):
    def test_angles(self):
        self.assertAlmostEqual(bend((0, 0, 0), (1, 0, 0), (2, 0, 0)), 0)
        self.assertAlmostEqual(bend((0, 0, 0), (1, 0, 0), (1, 1, 0)), 0.5)

    def test_rigid_transform_invariance(self):
        points = [(0.0, 0.0, 0.0), (-0.5, 0.2, 0), (-0.8, 0.5, 0),
                  (-1, 0.8, 0), (-1.2, 1, 0)]
        for x in (-0.4, 0.0, 0.4, 0.8):
            points.extend([(x, 1, 0), (x, 2, 0), (x, 2.5, 0.5), (x, 2.4, 1)])
        transformed = [(10 - 3*y, -4 + 3*x, 2 + 3*z) for x, y, z in points]
        for a, b in zip(raw_flexions(points), raw_flexions(transformed)):
            self.assertAlmostEqual(a, b)

    def test_invalid_landmarks(self):
        for points in ([(0, 0, 0)]*21, [(math.nan, 0, 0)]*21, []):
            with self.assertRaises(ValueError):
                raw_flexions(points)

    def test_calibrated_clamping(self):
        config = load_config(ROOT / "config/hand.json")
        for entry in config["calibration"].values():
            entry.update(open=0.2, closed=0.6)
        values = normalize([0, 0.2, 0.4, 0.6, 1], config["calibration"])
        for actual, expected in zip(values, [0, 0, 0.5, 1, 1]):
            self.assertAlmostEqual(actual, expected)


def synthetic_hand(curl=0.0, thumb_out=True):
    """Mão no plano XY (palma de frente para a câmera); curl dobra os dedos em direção a +z."""
    points = [(0.0, 0.0, 0.0)]
    thumb = [(-0.5, 0.2), (-0.9, 0.5), (-1.2, 0.8), (-1.5, 1.0)] if thumb_out else \
            [(-0.3, 0.3), (-0.2, 0.7), (0.0, 0.9), (0.2, 1.0)]
    points.extend((x, y, 0.0) for x, y in thumb)
    for x in (-0.45, -0.15, 0.15, 0.45):
        base = (x, 1.0, 0.0)
        joints, angle = [base], 0.0
        for length in (0.5, 0.3, 0.25):
            angle += curl
            prev = joints[-1]
            joints.append((x, prev[1] + length * math.cos(angle), prev[2] + length * math.sin(angle)))
        points.extend(joints)
    return points


class OpenPalmTests(unittest.TestCase):
    def test_open_palm_detected(self):
        points = synthetic_hand()
        self.assertTrue(is_open_palm(points, raw_flexions(points)))

    def test_fist_rejected(self):
        points = synthetic_hand(curl=1.2)
        self.assertFalse(is_open_palm(points, raw_flexions(points)))

    def test_thumb_tucked_rejected(self):
        points = synthetic_hand(thumb_out=False)
        self.assertFalse(is_open_palm(points, raw_flexions(points)))

    def test_side_view_rejected(self):
        # Gira 90 graus em torno de Y: palma de perfil para a câmera.
        points = [(z, y, x) for x, y, z in synthetic_hand()]
        self.assertLess(palm_facing(points), 0.1)
        self.assertFalse(is_open_palm(points, raw_flexions(points)))

    def test_flexion_relative_to_reference(self):
        config = load_config(ROOT / "config/hand.json")
        reference = [0.1] * 5
        span = [config["calibration"][f]["closed"] - config["calibration"][f]["open"] for f in config["calibration"]]
        values = normalize_from_open([0.1, 0.0, 0.1 + span[2] / 2, 5, 0.1], reference, config["calibration"])
        for actual, expected in zip(values, [0, 0, 0.5, 1, 0]):
            self.assertAlmostEqual(actual, expected)


class OpenPalmLockTests(unittest.TestCase):
    def test_locks_after_hold(self):
        lock = OpenPalmLock(hold_seconds=0.5)
        result = None
        for i in range(20):
            result = lock.update(i * 0.05, [0.1, 0.1, 0.1, 0.1, 0.1 + (i % 2) * 0.01], True)
            if result:
                break
        self.assertIsNotNone(result)
        self.assertGreaterEqual(i * 0.05, 0.5)
        self.assertAlmostEqual(result[0], 0.1)

    def test_closed_hand_resets(self):
        lock = OpenPalmLock(hold_seconds=0.5)
        for i in range(8):
            lock.update(i * 0.05, [0.1] * 5, True)
        lock.update(0.4, [0.1] * 5, False)
        self.assertEqual(lock.progress(0.4), 0)
        self.assertIsNone(lock.update(0.6, [0.1] * 5, True))

    def test_movement_restarts_count(self):
        lock = OpenPalmLock(hold_seconds=0.5)
        for i in range(10):
            lock.update(i * 0.05, [0.1] * 5, True)
        self.assertIsNone(lock.update(0.5, [0.3] * 5, True))
        self.assertAlmostEqual(lock.progress(0.5), 0)


class FilterTests(unittest.TestCase):
    def test_time_based_smoothing(self):
        a, b = Smoother(0.1), Smoother(0.1)
        a.update([0]*5, 0)
        b.update([0]*5, 0)
        one_step = a.update([1]*5, 0.1)
        b.update([1]*5, 0.05)
        two_steps = b.update([1]*5, 0.1)
        self.assertAlmostEqual(one_step[0], two_steps[0])
        self.assertGreater(one_step[0], 0)
        self.assertLess(one_step[0], 1)

    def test_reset(self):
        smoother = Smoother(0.1)
        smoother.update([0]*5, 0)
        smoother.reset()
        self.assertEqual(smoother.update([1]*5, 10), [1]*5)


class ConfigTests(unittest.TestCase):
    def test_non_numeric_calibration(self):
        config = load_config(ROOT / "config/hand.json")
        config["calibration"]["thumb"]["open"] = "0"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "hand.json"
            save_config(path, config)
            with self.assertRaises(ValueError):
                load_config(path)

    def test_bad_calibration(self):
        config = load_config(ROOT / "config/hand.json")
        config["calibration"]["thumb"]["closed"] = 0
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "hand.json"
            save_config(path, config)
            with self.assertRaises(ValueError):
                load_config(path)

    def test_old_profile_gets_defaults(self):
        config = load_config(ROOT / "config/hand.json")
        for key in ("open_hold_seconds", "open_max_bend", "open_min_facing"):
            del config[key]
        config["hand"] = "Right"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "hand.json"
            save_config(path, config)
            self.assertEqual(load_config(path)["open_hold_seconds"], 0.8)

    def test_save_round_trip(self):
        config = load_config(ROOT / "config/hand.json")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "hand.json"
            save_config(path, config)
            self.assertEqual(load_config(path), config)


if __name__ == "__main__":
    unittest.main()
