import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from mao_robotica.config import load_config, save_config
from mao_robotica.finger_mapping import bend, normalize, raw_flexions
from mao_robotica.filters import Smoother
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

    def test_save_round_trip(self):
        config = load_config(ROOT / "config/hand.json")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "hand.json"
            save_config(path, config)
            self.assertEqual(load_config(path), config)


if __name__ == "__main__":
    unittest.main()
