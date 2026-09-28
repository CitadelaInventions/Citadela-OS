import base64
import io
from pathlib import Path
import tempfile
import unittest
from PIL import Image

from assets import Assets, fnv1a
from protocol import Display, packet, ProtocolError


class AssetTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        bmp = io.BytesIO()
        Image.new("RGB", (4, 4), "white").save(bmp, format="BMP")
        self.bytes = bmp.getvalue()
        self.identity = fnv1a(self.bytes)
        self.d = Display(self.directory.name)
        self.sequence = 0

    def send(self, payload):
        self.sequence += 1
        self.d.feed(packet(self.sequence, payload))

    def transfer(self):
        self.send("H 20 16;")
        self.send(f"ASSET {self.identity:08X} {len(self.bytes)};")
        self.assertEqual(self.d.asset_reply, 0)
        self.send(f"DATA {self.identity:08X} 0 {base64.b64encode(self.bytes).decode()};")
        self.send(f"ASSETEND {self.identity:08X};")

    def test_file_transfers_and_persists_without_rendering_any_pixels(self):
        self.transfer()
        self.assertEqual(self.d.image.getpixel((0, 0)), (0, 0, 0))
        path = Path(self.directory.name) / f"{self.identity:08X}.bmp"
        self.assertEqual(path.read_bytes(), self.bytes)
        second = Assets(self.directory.name)
        self.assertTrue(second.begin(self.identity, len(self.bytes)))

    def test_bitmap_draws_only_requested_region(self):
        self.transfer()
        self.d.palette[31] = (255, 255, 255)
        self.send(f"BMP {self.identity:08X} 0 2 3 8 8 2 1 3 2 100 0 1 0 0 255 0 0 0 255 0 1;E;")
        self.assertEqual(self.d.image.getpixel((4, 4)), (255, 255, 255))
        self.assertEqual(self.d.image.getpixel((3, 4)), (0, 0, 0))
        self.assertEqual(self.d.image.getpixel((7, 4)), (0, 0, 0))

    def test_recording_replays_when_asset_is_already_cached(self):
        self.transfer()
        cached = Display(self.directory.name)
        cached.feed(packet(1, "H 20 16;"))
        cached.feed(packet(2, f"ASSET {self.identity:08X} {len(self.bytes)};"))
        self.assertEqual(cached.asset_reply, self.identity)
        cached.feed(packet(3, f"DATA {self.identity:08X} 0 {base64.b64encode(self.bytes).decode()};"))
        cached.feed(packet(4, f"ASSETEND {self.identity:08X};E;"))
        self.assertTrue(cached.synced)

    def test_deferred_path_alias_renders_the_transferred_bitmap(self):
        self.transfer()
        alias = 0x12345678
        self.send(f"ASSETREF {alias:08X} {self.identity:08X};")
        self.d.palette[31] = (255, 255, 255)
        self.send(f"BMP {alias:08X} 0 2 3 8 8 0 0 8 8 100 0 1 0 0 255 0 0 0 255 0 1;E;")
        self.assertEqual(self.d.presented.getpixel((2, 3)), (255, 255, 255))
        self.send("H 20 16;")
        self.assertEqual(self.d.asset_aliases, {})

    def test_deferred_alias_rejects_missing_asset(self):
        with self.assertRaises(ProtocolError):
            self.send("ASSETREF 12345678 99887766;")

    def test_cached_alias_finishes_without_waiting_for_file_chunks(self):
        self.transfer()
        self.send(f"ASSET {self.identity:08X} {len(self.bytes)};")
        self.assertEqual(self.d.asset_reply, self.identity)
        self.send(f"ASSETREF 12345678 {self.identity:08X};")
        self.assertFalse(self.d.assets.pending)

    def test_bottom_up_wallpaper_scaling_matches_row_replication(self):
        image = Image.new("RGB", (1, 2))
        image.putdata([(0, 0, 0), (255, 255, 255)])
        raw = io.BytesIO()
        image.save(raw, format="BMP")
        data = raw.getvalue()
        identity = fnv1a(data)
        assets = Assets()
        assets.begin(identity, len(data))
        assets.chunk(identity, 0, data)
        assets.finish(identity)
        tables = {i: list(range(256)) for i in range(1, 6)}
        palette = [(i, i, i) for i in range(256)]
        out, _ = assets.bitmap([identity, 0, 0, 0, 1, 3, 0, 0, 1, 3, 100, 0, 1,
                                0, 0, 255, 0, 0, 0, 256, 0, 1], tables, palette)
        self.assertEqual(out.getpixel((0, 1))[:3], (0, 0, 0))
        self.assertEqual(out.getpixel((0, 2))[:3], (31, 31, 31))

    def test_corrupted_file_rejected(self):
        assets = Assets()
        assets.begin(self.identity, len(self.bytes))
        assets.chunk(self.identity, 0, self.bytes[:-1] + b"x")
        with self.assertRaises(ValueError):
            assets.finish(self.identity)

    def test_wrong_file_offset_rejected(self):
        assets = Assets()
        assets.begin(self.identity, len(self.bytes))
        with self.assertRaises(ValueError):
            assets.chunk(self.identity, 20, b"abc")

    def test_frame_updates_are_committed_in_batches(self):
        self.send("H 20 16;FILL 0 0 2 2 FFFFFFFF;")
        self.assertEqual(self.d.revision, 0)
        self.send("E;")
        revision = self.d.revision
        committed = self.d.presented.tobytes()
        self.send("RECT 2 2 4 4 FF00FF00;")
        self.assertEqual(self.d.revision, revision)
        self.assertEqual(self.d.presented.tobytes(), committed)
        self.send("FRAME;")
        self.assertEqual(self.d.revision, revision+1)
        self.assertNotEqual(self.d.presented.tobytes(), committed)
        self.send("FILL 0 0 20 16 FF0000FF;")
        self.assertEqual(self.d.presented.getpixel((0, 0)), (255, 255, 255))

    def test_cursor_and_drag_do_not_modify_the_desktop(self):
        self.send("H 20 16;FILL 0 0 20 16 FFFFFFFF;CURSOR 10 8 1 0;DRAG 1 2 2 6 6;E;")
        original = self.d.image.tobytes()
        self.assertEqual(self.d.composited().getpixel((8, 7)), (0, 0, 0))
        self.send("CURSOR 0 0 0 0;DRAG 0 0 0 0 0;FRAME;")
        self.assertEqual(self.d.composited().tobytes(), original)

    def test_overlays_use_the_firmware_calibrated_colors(self):
        self.send("H 20 16;CURSOR 10 8 1 1 FFEDBADD FF030201;DRAG 1 2 2 6 6 FF123456;E;")
        image = self.d.presented
        self.assertEqual(image.getpixel((13, 8)), (1, 2, 3))
        self.assertEqual(image.getpixel((14, 8)), (221, 186, 237))
        self.assertEqual(image.getpixel((2, 2)), (86, 52, 18))


if __name__ == "__main__":
    unittest.main()
