"""PCKNOB-06 host suite for pc_media_protocol.

Runs under pytest or the standard library runner, with no Windows packages installed:

    python3 -m pytest esp-idf/PC/tests
    python3 -m unittest discover -s esp-idf/PC/tests -v
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

import pc_media_protocol as protocol  # noqa: E402


class ImportPurity(unittest.TestCase):
    def test_module_pulls_no_windows_or_broker_packages(self):
        for name in ("win32gui", "win32api", "win32con", "pycaw", "comtypes", "paho"):
            self.assertNotIn(name, sys.modules)


class SenderFraming(unittest.TestCase):
    def test_split_sender_prefix_form(self):
        self.assertEqual(protocol.split_sender("sender=QNOB,play"), ("QNOB", "play"))

    def test_split_sender_anywhere_in_the_list(self):
        self.assertEqual(protocol.split_sender("state:playing,sender=PC,volume:42"),
                         ("PC", "state:playing,volume:42"))

    def test_split_sender_without_sender(self):
        self.assertEqual(protocol.split_sender("play"), (None, "play"))
        self.assertEqual(protocol.split_sender(""), (None, ""))

    def test_is_self_is_exact(self):
        self.assertTrue(protocol.is_self("sender=PC,play", "PC"))
        self.assertFalse(protocol.is_self("sender=PCX,play", "PC"))
        self.assertFalse(protocol.is_self("play", "PC"))

    def test_with_sender_is_idempotent(self):
        framed = protocol.with_sender("PC", "playing")
        self.assertEqual(framed, "sender=PC,playing")
        self.assertEqual(protocol.with_sender("PC", framed), framed)


class Control(unittest.TestCase):
    def test_the_four_commands(self):
        for command in ("play", "pause", "forward", "rewind"):
            self.assertEqual(protocol.parse_control(command), command)
            self.assertEqual(protocol.parse_control(f"  {command}\n"), command)

    def test_unknown_commands_are_none(self):
        for text in ("stop", "PLAY", "", "play,now", "next"):
            self.assertIsNone(protocol.parse_control(text))


class Setpoint(unittest.TestCase):
    def test_mqtt_and_serial_forms(self):
        self.assertEqual(protocol.parse_setpoint("setpoint:42"), 42)
        self.assertEqual(protocol.parse_setpoint("setpoint=7"), 7)
        self.assertEqual(protocol.parse_setpoint("setpoint:100,extra"), 100)
        self.assertEqual(protocol.parse_setpoint("setpoint:0"), 0)

    def test_rejections(self):
        for text in ("setpoint:abc", "setpoint:", "setpoint:101", "setpoint:-1", "volume:42",
                     "setpoint:99999", "setpoint: 1e2", ""):
            self.assertIsNone(protocol.parse_setpoint(text), text)

    def test_serial_setpoint_format(self):
        self.assertEqual(protocol.format_serial_setpoint(42), "setpoint=42")
        self.assertEqual(protocol.format_serial_setpoint(150), "setpoint=100")
        self.assertEqual(protocol.format_serial_setpoint(-3), "setpoint=0")


class State(unittest.TestCase):
    def test_request_detection(self):
        self.assertTrue(protocol.is_state_request("request"))
        self.assertTrue(protocol.is_state_request("state request"))
        self.assertFalse(protocol.is_state_request("play"))

    def test_format_and_parse_round_trip(self):
        text = protocol.format_state(True, 42)
        self.assertEqual(text, "state:playing,volume:42")
        self.assertEqual(protocol.parse_state(text), {"playing": True, "volume": 42})
        self.assertEqual(protocol.format_state(False, 250), "state:paused,volume:100")

    def test_bare_states(self):
        self.assertEqual(protocol.format_play_state(True), "playing")
        self.assertEqual(protocol.parse_state("paused"), {"playing": False, "volume": None})

    def test_malformed_states(self):
        for text in ("state:maybe,volume:1", "volume:42", "state:playing,volume:x", "garbage", ""):
            self.assertIsNone(protocol.parse_state(text), text)


class SerialLines(unittest.TestCase):
    def test_lines(self):
        self.assertEqual(protocol.parse_serial_line("setpoint=33\r\n"), ("setpoint", 33))
        self.assertEqual(protocol.parse_serial_line("playing"), ("state", True))
        self.assertEqual(protocol.parse_serial_line("paused"), ("state", False))
        self.assertIsNone(protocol.parse_serial_line("setpoint=abc"))
        self.assertIsNone(protocol.parse_serial_line("ota:status"))


if __name__ == "__main__":
    unittest.main()
