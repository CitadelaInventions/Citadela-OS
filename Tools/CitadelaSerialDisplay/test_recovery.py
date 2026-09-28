import unittest

from recovery import ScreenSync


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.sync = ScreenSync(0)

    def scene(self, now=0):
        self.sync.requested(now)
        self.sync.received([("H", (376, 288)), ("E", ())], now)

    def test_no_requests_before_connecting_or_when_disabled(self):
        self.assertIsNone(self.sync.due(100))
        self.scene()
        self.sync.disabled()
        self.assertIsNone(self.sync.due(100))

    def test_periodic_refresh_even_when_clock_keeps_updating(self):
        self.scene()
        for now in range(1, 30):
            self.sync.received([("T", ()), ("FRAME", ())], now)
            self.assertIsNone(self.sync.due(now))
        self.assertEqual(self.sync.due(30), "periodic")

    def test_waits_for_held_keys_mouse_and_input_idle(self):
        self.scene()
        self.sync.input("VDM INPUT key down", 28)
        self.assertIsNone(self.sync.due(30))
        self.sync.input("VDM INPUT key rlsd", 30)
        self.assertIsNone(self.sync.due(31))
        self.sync.input("VDM INPUT mouse 12 34 1", 32)
        self.assertIsNone(self.sync.due(35))
        self.sync.input("VDM INPUT mouse 12 34 0", 35)
        self.assertIsNone(self.sync.due(36))
        self.assertEqual(self.sync.due(37), "periodic")

    def test_long_file_transfer_with_progress_is_not_interrupted(self):
        self.sync.requested(0)
        self.sync.received([("H", ())], 0)
        for now in range(1, 100):
            self.sync.received([("DATA", ())], now)
            self.assertIsNone(self.sync.due(now))
        self.sync.received([("ASSETEND", ()), ("BMP", ()), ("E", ())], 100)
        self.assertIsNone(self.sync.due(129))
        self.assertEqual(self.sync.due(130), "periodic")

    def test_stalled_initial_scene_retries_without_flooding(self):
        self.sync.requested(0)
        self.assertIsNone(self.sync.due(11))
        self.assertEqual(self.sync.due(12), "stalled")
        self.sync.requested(12)
        self.assertIsNone(self.sync.due(23))
        self.assertEqual(self.sync.due(24), "stalled")

    def test_partial_redraw_recovers_after_progress_stops(self):
        self.scene()
        self.sync.received([("F", ())], 5)
        self.assertIsNone(self.sync.due(16))
        self.assertEqual(self.sync.due(17), "stalled")

    def test_protocol_failure_recovers_even_without_open_frame(self):
        self.scene()
        self.sync.damaged = True
        self.assertEqual(self.sync.due(12), "stalled")

    def test_periodic_refresh_does_not_interrupt_transport_recovery(self):
        self.scene()
        self.sync.last_progress = 29
        self.sync.damaged = True
        self.assertIsNone(self.sync.due(30))
        self.assertEqual(self.sync.due(41), "stalled")

    def test_old_frame_does_not_complete_a_requested_scene(self):
        self.scene()
        self.sync.requested(30)
        self.sync.received([("FRAME", ())], 31)
        self.assertTrue(self.sync.pending)
        self.assertIsNone(self.sync.due(31))

    def test_lost_request_recovers_despite_regular_clock_packets(self):
        self.scene()
        self.sync.requested(30)
        for now in range(31, 42):
            self.sync.received([("T", ()), ("FRAME", ())], now)
            self.assertIsNone(self.sync.due(now))
        self.assertEqual(self.sync.due(42), "stalled")

    def test_previous_scene_end_does_not_complete_a_new_request(self):
        self.scene()
        self.sync.requested(30)
        self.sync.received([("E", ())], 31)
        self.assertTrue(self.sync.pending)
        self.assertEqual(self.sync.due(42), "stalled")

    def test_disabled_command_cancels_all_recovery(self):
        self.scene()
        self.sync.received([("STATE", (0,)), ("FRAME", ())], 1)
        self.assertIsNone(self.sync.due(100))

    def test_recovery_releases_input_at_existing_mouse_position(self):
        self.sync.input("VDM INPUT mouse 15 70 1", 0)
        self.sync.input("VDM INPUT key enter", 0)
        self.assertEqual(self.sync.release_commands(),
                         ["VDM INPUT mouse 15 70 0", "VDM INPUT key rlsd"])
        self.assertFalse(self.sync.buttons)
        self.assertFalse(self.sync.key_down)

    def test_period_starts_after_each_completed_resync(self):
        self.scene()
        self.sync.requested(30)
        self.assertIsNone(self.sync.due(35))
        self.sync.received([("H", ()), ("E", ())], 36)
        self.assertIsNone(self.sync.due(65))
        self.assertEqual(self.sync.due(66), "periodic")


if __name__ == "__main__":
    unittest.main()
