import threading
import queue
import tkinter as tk
from types import SimpleNamespace
import unittest

from viewer import Viewer, serial_port_names
from protocol import Display, packet


class FakeConnection:
    def __init__(self):
        self.ready = True
        self.lock = threading.Lock()
        self.motion = None
        self.commands = []

    def send(self, line):
        self.commands.append(line)


class ViewerTests(unittest.TestCase):
    def setUp(self):
        self.root = tk.Tk()
        self.viewer = Viewer(self.root)
        self.root.update()
        self.root.update_idletasks()
        self.viewer.connection = FakeConnection()

    def tearDown(self):
        for timer in self.root.tk.call("after", "info"):
            self.root.after_cancel(timer)
        self.root.destroy()

    def test_mouse_coordinates_respect_letterboxing(self):
        self.viewer.render()
        x, y, sx, sy = self.viewer.geometry
        event = SimpleNamespace(x=x+120*sx+sx/2, y=y+80*sy+sy/2)
        self.assertEqual(self.viewer.position(event), (120, 80))
        self.assertIsNone(self.viewer.position(SimpleNamespace(x=-1, y=-1)))

    def test_click_and_release_are_preserved(self):
        self.viewer.render()
        x, y, sx, sy = self.viewer.geometry
        event = SimpleNamespace(x=x+10*sx+sx/2, y=y+10*sy+sy/2)
        self.viewer.mouse_button(event, 1, True)
        self.viewer.mouse_button(event, 1, False)
        self.assertEqual(self.viewer.connection.commands,
                         ["VDM INPUT mouse 10 10 1", "VDM INPUT mouse 10 10 0"])

    def test_keyboard_and_space(self):
        self.viewer.key(SimpleNamespace(keysym="Up", char=""))
        self.viewer.key(SimpleNamespace(keysym="space", char=" "))
        self.viewer.key(SimpleNamespace(keysym="a", char="a"))
        self.viewer.key_release(None)
        self.assertEqual(self.viewer.connection.commands, ["VDM INPUT key up",
            "VDM INPUT key Space", "VDM INPUT key a", "VDM INPUT key rlsd"])

    def test_input_disabled_until_initial_snapshot_completes(self):
        self.viewer.connection.ready = False
        self.viewer.key(SimpleNamespace(keysym="Return", char=""))
        self.viewer.key_release(None)
        self.assertEqual(self.viewer.connection.commands, [])

    def test_tk_render_does_not_hold_the_receiver_lock(self):
        conn = self.viewer.connection
        conn.events = queue.Queue()
        conn.display = Display()
        conn.display.feed(packet(1, "H 20 16;E;"))
        conn.bytes = conn.errors = 0
        rendered = []

        def render():
            self.assertTrue(conn.lock.acquire(blocking=False))
            conn.lock.release()
            rendered.append(True)

        self.viewer.render = render
        self.viewer.tick()
        self.assertEqual(rendered, [True])

    def test_macos_ports_prefer_callout_device_and_usb(self):
        ports = [SimpleNamespace(device="/dev/tty.usbmodem2101"),
                 SimpleNamespace(device="/dev/cu.Bluetooth-Incoming-Port"),
                 SimpleNamespace(device="/dev/cu.usbmodem2101")]
        self.assertEqual(serial_port_names(ports, "darwin"),
                         ["/dev/cu.usbmodem2101", "/dev/cu.Bluetooth-Incoming-Port"])

    def test_port_names_remain_unchanged_on_windows(self):
        ports = [SimpleNamespace(device="COM10"), SimpleNamespace(device="COM4")]
        self.assertEqual(serial_port_names(ports, "win32"), ["COM10", "COM4"])


if __name__ == "__main__":
    unittest.main()
