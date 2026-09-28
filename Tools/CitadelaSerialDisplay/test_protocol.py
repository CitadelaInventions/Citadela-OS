import unittest
from protocol import Display, ProtocolError, packet


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.d = Display()
        self.seq = 0
        self.send("H 20 16;")

    def send(self, payload):
        self.seq += 1
        return self.d.feed(packet(self.seq, payload))

    def test_palette_and_bitmap_spans(self):
        self.send("P 0 FF000000FF00FF00FF0000FF;B 1 2 000102;R 4 2 3 1;E;")
        self.assertEqual(self.d.image.getpixel((2, 2)), (0, 255, 0))
        self.assertEqual(self.d.image.getpixel((3, 2)), (255, 0, 0))
        self.assertEqual(self.d.image.getpixel((6, 2)), (0, 255, 0))
        self.assertTrue(self.d.synced)

    def test_rectangles_clip_without_off_by_one(self):
        self.send("F -2 -1 5 4 FFFFFFFF;O 5 5 4 4 FF0000FF;")
        self.assertEqual(self.d.image.getpixel((2, 2)), (255, 255, 255))
        self.assertEqual(self.d.image.getpixel((3, 2)), (0, 0, 0))
        self.assertEqual(self.d.image.getpixel((8, 8)), (255, 0, 0))
        self.assertEqual(self.d.image.getpixel((6, 6)), (0, 0, 0))

    def test_compressed_palette_span(self):
        self.send("P 0 FF000000FF00FF00;D 1 2 03000201;")
        self.assertEqual(self.d.image.getpixel((3, 2)), (0, 0, 0))
        self.assertEqual(self.d.image.getpixel((4, 2)), (0, 255, 0))
        with self.assertRaises(ProtocolError):
            self.d.feed(packet(3, "D 0 0 FF01;"))

    def test_transparent_glyph(self):
        self.send("F 0 0 20 16 FF00FF00;T 1 1 2 2 FFFFFFFF 00000000 65 9;")
        self.assertEqual(self.d.image.getpixel((1, 1)), (255, 255, 255))
        self.assertEqual(self.d.image.getpixel((2, 1)), (0, 255, 0))
        self.assertEqual(self.d.text[-1], (1, 1, "A"))

    def test_scroll_both_directions(self):
        self.send("F 0 2 4 1 FFFFFFFF;S 2 FF0000FF;")
        self.assertEqual(self.d.image.getpixel((0, 0)), (255, 255, 255))
        self.assertEqual(self.d.image.getpixel((0, 15)), (255, 0, 0))
        self.send("S -2 FF00FF00;")
        self.assertEqual(self.d.image.getpixel((0, 0)), (0, 255, 0))
        self.assertEqual(self.d.image.getpixel((0, 2)), (255, 255, 255))

    def test_line_matches_driver_tie_rules(self):
        self.send("L 1 1 5 3 FFFFFFFF;")
        white = {(x, y) for y in range(16) for x in range(20)
                 if self.d.image.getpixel((x, y)) == (255, 255, 255)}
        self.assertEqual(white, {(1, 1), (2, 2), (3, 2), (4, 3), (5, 3)})

    def test_circle_has_complete_top_arc(self):
        self.send("C 7 7 4 0 FFFFFFFF;")
        self.assertEqual(self.d.image.getpixel((5, 3)), (255, 255, 255))
        self.assertEqual(self.d.image.getpixel((7, 7)), (0, 0, 0))

    def test_crc_rejects_packet_without_modifying_image(self):
        before = self.d.image.tobytes()
        with self.assertRaises(ProtocolError):
            self.d.feed(packet(2, "F 0 0 20 16 FFFFFFFF;").replace("FFFFFF", "FF00FF"))
        self.assertEqual(self.d.image.tobytes(), before)
        self.assertEqual(self.d.sequence, 1)

    def test_duplicate_ignored_and_gap_detected(self):
        line = packet(2, "F 0 0 2 2 FFFFFFFF;")
        self.assertTrue(self.d.feed(line))
        self.assertFalse(self.d.feed(line))
        with self.assertRaises(ProtocolError):
            self.d.feed(packet(4, "E;"))

    def test_snapshot_resets_sequence(self):
        self.send("F 0 0 20 16 FFFFFFFF;E;")
        self.d.feed(packet(1, "H 10 8;"))
        self.assertFalse(self.d.synced)
        self.assertEqual(self.d.image.size, (10, 8))
        self.assertEqual(self.d.image.getpixel((0, 0)), (0, 0, 0))

    def test_invalid_commands_and_size(self):
        for payload in ("H 5000 5000;", "Q 2;", "T 0 0 2 2 FFFF FFFF 65 ZZ;", "R 0 0 800 2;"):
            with self.assertRaises(ProtocolError):
                self.d.feed(packet(2, payload))

    def test_regular_serial_logs_ignored(self):
        self.assertFalse(self.d.feed("BLE reconnect pending"))


if __name__ == "__main__":
    unittest.main()
