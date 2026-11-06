from pathlib import Path
import sys
import time
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from mao_robotica.protocol import decode, encode
from mao_robotica.serial_client import SerialClient


class FakeSerial:
    def __init__(self, *args, **kwargs):
        self.received = bytearray()
        self.commands = []
        self.closed = False
        self.acknowledge = True

    @property
    def in_waiting(self):
        return len(self.received)

    def read(self, count):
        result = bytes(self.received[:count])
        del self.received[:count]
        return result

    def write(self, frame):
        command = decode(frame)
        self.commands.append(command)
        response = {"HELLO": "READY,1", "ARM": "ARMED", "STOP": "DISARMED"}.get(command)
        if command.startswith("SET,") and self.acknowledge:
            response = "ACK," + command.split(",")[1]
        if response:
            self.received.extend(encode(response))
        return len(frame)

    def close(self):
        self.closed = True


class SerialTests(unittest.TestCase):
    def setUp(self):
        self.patch = patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=FakeSerial)})
        self.patch.start()
        self.client = SerialClient("TEST")

    def tearDown(self):
        self.client.close()
        self.patch.stop()

    def test_explicit_arm(self):
        self.client.send([0]*5)
        self.assertEqual(self.client.serial.commands, ["HELLO"])
        self.client.arm()
        self.client.send([0, 0.25, 0.5, 0.75, 1])
        self.assertEqual(self.client.serial.commands[-1], "SET,0,0,250,500,750,1000")

    def test_lost_ack_stops(self):
        self.client.arm()
        self.client.last_ack = time.monotonic() - 1
        with self.assertRaises(RuntimeError):
            self.client.send([0]*5)
        self.assertFalse(self.client.armed)
        self.assertEqual(self.client.serial.commands[-1], "STOP")

    def test_board_reset_requires_rearm(self):
        self.client.arm()
        self.client.serial.received.extend(encode("READY,1"))
        self.client.send([1]*5)
        self.assertFalse(self.client.armed)
        self.assertFalse(any(c.startswith("SET,") for c in self.client.serial.commands))

    def test_fragmented_reply(self):
        frame = encode("ACK,3")
        self.client.serial.received.extend(frame[:4])
        self.assertEqual(self.client.poll(), [])
        self.client.serial.received.extend(frame[4:])
        self.assertEqual(self.client.poll(), ["ACK,3"])

    def test_fault(self):
        self.client.arm()
        self.client.serial.received.extend(encode("FAULT,TIMEOUT"))
        with self.assertRaises(RuntimeError):
            self.client.poll()
        self.assertFalse(self.client.armed)

    def test_unrelated_ack_does_not_refresh_watchdog(self):
        self.client.arm()
        before = self.client.last_ack
        self.client.serial.received.extend(encode("ACK,999"))
        self.client.poll()
        self.assertEqual(before, self.client.last_ack)

    def test_ack_for_sent_command(self):
        self.client.arm()
        self.client.send([0]*5)
        self.assertEqual(self.client.pending, [0])
        self.client.poll()
        self.assertEqual(self.client.pending, [])


if __name__ == "__main__":
    unittest.main()
