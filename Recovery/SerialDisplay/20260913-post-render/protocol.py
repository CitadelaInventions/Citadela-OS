"""VDM1 drawing-command decoder, independent of the GUI and serial port."""

import binascii
import base64
import math
from PIL import Image, ImageDraw
from assets import Assets, fnv1a


class ProtocolError(ValueError):
    pass


def packet(sequence, payload):
    data = payload.encode("ascii")
    return f"VDM1 {sequence} {binascii.crc_hqx(data, 0xffff):04X} {payload}"


def rgb(value):
    return (value & 255, (value >> 8) & 255, (value >> 16) & 255)


class Display:
    def __init__(self, asset_directory=None):
        self.image = Image.new("RGB", (376, 285))
        self.presented = self.image.copy()
        self.palette = [(i, i, i) for i in range(256)]
        self.sequence = 0
        self.commands = 0
        self.revision = 0
        self.synced = False
        self.text = []
        self.assets = Assets(asset_directory)
        self.tables = {i: list(range(256)) for i in range(1, 6)}
        self.asset_reply = 0
        self.enabled = True
        self.cursor = None
        self.drag = None
        self.last_records = []
        self.checks = []

    def feed(self, line):
        """Apply a checked, ordered packet; ignore retransmitted packets."""
        start = line.find("VDM1 ")
        if start < 0:
            return False
        try:
            _, seq, crc, payload = line[start:].strip().split(" ", 3)
            seq = int(seq)
            expected = int(crc, 16)
            if len(payload) > 560 or not payload.endswith(";"):
                raise ProtocolError("Invalid packet length")
            if binascii.crc_hqx(payload.encode("ascii"), 0xffff) != expected:
                raise ProtocolError("Checksum mismatch")
            fresh = seq == 1 and payload.startswith("H ")
            if not fresh and seq == self.sequence:
                return False
            if not fresh and seq != self.sequence + 1:
                raise ProtocolError(f"Missing drawing packet: {self.sequence} -> {seq}")
            records = [self._parse(record) for record in payload[:-1].split(";")]
            self.asset_reply = 0
            for op, args in records:
                self._apply(op, args)
            self.sequence = seq
            self.commands += len(records)
            self.last_records = records
            return True
        except (ValueError, IndexError, UnicodeError) as exc:
            if isinstance(exc, ProtocolError):
                raise
            raise ProtocolError(f"Malformed drawing packet: {exc}") from exc

    def _parse(self, record):
        parts = record.split()
        if not parts:
            raise ProtocolError("Empty drawing command")
        op, args = parts[0], parts[1:]
        op = {"RECT": "O", "FILL": "F", "LINE": "L", "CIRCLE": "C", "GLYPH": "T", "SCROLL": "S"}.get(op, op)
        counts = {"H": 2, "P": 2, "F": 5, "O": 5, "L": 5,
                  "C": 5, "T": 8, "S": 2, "B": 3, "D": 3, "R": 4, "E": 0,
                  "LUT": 3, "ASSET": 2, "DATA": 3, "ASSETEND": 1, "BMP": 22,
                  "CURSOR": 4, "DRAG": 5, "FRAME": 0, "STATE": 1, "CHECK": 5}
        if op in ("CURSOR", "DRAG") and len(args) in (counts[op], 6):
            numbers = [int(v) for v in args[:counts[op]]]
            colors = [int(v, 16) for v in args[counts[op]:]]
            return op, numbers + colors
        if op not in counts or len(args) != counts[op]:
            raise ProtocolError(f"Unknown or incomplete command: {op}")
        if op == "LUT":
            slot, offset = map(int, args[:2])
            values = bytes.fromhex(args[2])
            if slot not in self.tables or not 0 <= offset <= 256-len(values):
                raise ProtocolError("Invalid colour lookup table")
            return op, (slot, offset, values)
        if op in ("ASSET", "ASSETEND", "DATA", "BMP"):
            identity = int(args[0], 16)
            if op == "ASSET":
                return op, (identity, int(args[1]))
            if op == "ASSETEND":
                return op, (identity,)
            if op == "DATA":
                data = base64.b64decode(args[2], validate=True)
                if not 1 <= len(data) <= 384:
                    raise ProtocolError("Invalid file chunk")
                return op, (identity, int(args[1]), data)
            values = [identity] + [int(v) for v in args[1:]]
            if not (0 <= values[1] <= 2 and 0 < values[4] <= 1024 and 0 < values[5] <= 1024):
                raise ProtocolError("Invalid bitmap properties")
            return op, values
        if op == "P":
            offset = int(args[0])
            data = args[1]
            if len(data) % 8 or not (0 <= offset <= 256 - len(data) // 8):
                raise ProtocolError("Invalid palette")
            return op, (offset, [rgb(int(data[i:i+8], 16)) for i in range(0, len(data), 8)])
        if op in ("B", "D"):
            x, y = map(int, args[:2])
            data = bytes.fromhex(args[2])
            if op == "D":
                if len(data) % 2 or any(n == 0 for n in data[0::2]) or sum(data[0::2]) > 48:
                    raise ProtocolError("Invalid compressed bitmap span")
                data = bytes(index for count, index in zip(data[0::2], data[1::2]) for _ in range(count))
            if not 1 <= len(data) <= 48:
                raise ProtocolError("Invalid bitmap span")
            return "B", (x, y, data)
        if op == "T":
            x, y, w, h = map(int, args[:4])
            fg, bg = (int(c, 16) for c in args[4:6])
            char = int(args[6])
            # Older log readers can inspect the character without knowing its font.
            mask = args[7]
            if not (w > 0 and h > 0 and w*h <= 128 and len(mask) == (w*h+3)//4):
                raise ProtocolError("Invalid glyph")
            int(mask, 16)
            return op, (x, y, w, h, fg, bg, char, mask)
        if op in ("F", "O", "L", "C", "S", "CHECK"):
            values = [int(v) for v in args[:-1]] + [int(args[-1], 16)]
        else:
            values = [int(v) for v in args]
        if any(abs(v) > 8192 for v in values[:-1] if isinstance(v, int)):
            raise ProtocolError("Drawing coordinates out of range")
        if op == "H" and not all(1 <= v <= 1024 for v in values):
            raise ProtocolError("Unsupported resolution")
        if op == "R" and not (0 <= values[2] <= 48 and 0 <= values[3] < 256):
            raise ProtocolError("Invalid solid span")
        return op, values

    def _fill(self, x, y, w, h, color):
        if w > 0 and h > 0:
            ImageDraw.Draw(self.image).rectangle((x, y, x+w-1, y+h-1), fill=rgb(color))

    def _apply(self, op, a):
        draw = ImageDraw.Draw(self.image)
        if op == "H":
            self.image = Image.new("RGB", tuple(a))
            self.synced = False
            self.text.clear()
            self.assets.pending.clear()
            self.cursor = self.drag = None
            self.enabled = True
        elif op == "P":
            offset, colors = a
            self.palette[offset:offset+len(colors)] = colors
        elif op == "F":
            self._fill(*a)
        elif op == "O":
            x, y, w, h, c = a
            if w > 0 and h > 0:
                draw.rectangle((x, y, x+w-1, y+h-1), outline=rgb(c))
        elif op == "L":
            x0, y0, x1, y1, c = a
            dx, dy = x1-x0, y1-y0
            ax, ay = abs(dx), abs(dy)
            direction = 1 if dx*dy > 0 else -1
            if ay <= ax:
                if dx < 0:
                    x0, y0, x1, y1 = x1, y1, x0, y0
                error = 2*ay-ax
                draw.point((x0, y0), rgb(c))
                while x0 < x1:
                    x0 += 1
                    if error < 0:
                        error += 2*ay
                    else:
                        y0 += direction
                        error += 2*(ay-ax)
                    draw.point((x0, y0), rgb(c))
            else:
                if dy < 0:
                    x0, y0, x1, y1 = x1, y1, x0, y0
                error = 2*ax-ay
                draw.point((x0, y0), rgb(c))
                while y0 < y1:
                    y0 += 1
                    if error <= 0:
                        error += 2*ax
                    else:
                        x0 += direction
                        error += 2*(ax-ay)
                    draw.point((x0, y0), rgb(c))
        elif op == "C":
            x, y, r, filled, c = a
            if r < 0 or r > 1024:
                return
            old_extent = r
            for row in range(r+1):
                extent = math.isqrt(max(0, r*r-row*row))
                for yy in ([y+row, y-row] if row else [y]):
                    if filled:
                        self._fill(x-extent, yy, 2*extent+1, 1, c)
                    else:
                        self._fill(x-old_extent, yy, old_extent-extent+1, 1, c)
                        self._fill(x+extent, yy, old_extent-extent+1, 1, c)
                old_extent = extent
        elif op == "T":
            x, y, w, h, fg, bg, char, mask = a
            if w <= 0 or h <= 0 or w*h > 128 or len(mask) != (w*h+3)//4:
                raise ProtocolError("Invalid glyph")
            bits = "".join(f"{int(c, 16):04b}" for c in mask)
            for i, bit in enumerate(bits[:w*h]):
                color = fg if bit == "1" else bg
                alpha = color >> 24
                px, py = x+i%w, y+i//w
                if alpha and 0 <= px < self.image.width and 0 <= py < self.image.height:
                    out = rgb(color)
                    if alpha < 255:
                        old = self.image.getpixel((px, py))
                        out = tuple((b*(255-alpha) + f*(alpha+1)) >> 8 for b, f in zip(old, out))
                    draw.point((px, py), out)
            self.text.append((x, y, chr(char) if 0 <= char <= 0x10ffff else "?"))
            self.text = self.text[-2048:]
        elif op == "S":
            dy, c = a
            old = self.image.copy()
            self._fill(0, 0, self.image.width, self.image.height, c)
            self.image.paste(old, (0, -dy))
        elif op == "B":
            x, y, data = a
            for i, index in enumerate(data):
                draw.point((x+i, y), self.palette[index])
        elif op == "R":
            x, y, count, index = a
            if count:
                draw.rectangle((x, y, x+count-1, y), fill=self.palette[index])
        elif op == "E":
            self.synced = True
            self.presented = self.composited()
            self.revision += 1
        elif op == "FRAME":
            self.presented = self.composited()
            self.revision += 1
        elif op == "LUT":
            slot, offset, values = a
            self.tables[slot][offset:offset+len(values)] = values
        elif op == "ASSET":
            if self.assets.begin(*a):
                self.asset_reply = a[0]
        elif op == "DATA":
            self.assets.chunk(*a)
        elif op == "ASSETEND":
            self.assets.finish(*a)
        elif op == "BMP":
            image, position = self.assets.bitmap(a, self.tables, self.palette)
            self.image.paste(image, position, image)
        elif op == "CURSOR":
            x, y, visible, outline = a[:4]
            white, black = [rgb(v) for v in a[4:]] if len(a) == 6 else [(255, 255, 255), (0, 0, 0)]
            self.cursor = (x, y, outline, white, black) if visible else None
        elif op == "DRAG":
            visible, x, y, w, h = a[:5]
            color = rgb(a[5]) if len(a) == 6 else (255, 255, 255)
            self.drag = (x, y, w, h, color) if visible else None
        elif op == "STATE":
            self.enabled = bool(a[0])
            self.revision += 1
        elif op == "CHECK":
            x, y, w, h, expected = a
            actual = fnv1a(self.composited().crop((x, y, x+w, y+h)).tobytes())
            self.checks.append((x, y, w, h, actual, expected))

    def composited(self):
        image = self.image.copy()
        draw = ImageDraw.Draw(image)
        if self.drag:
            x, y, w, h, color = self.drag
            draw.rectangle((x, y, x+w-1, y+h-1), outline=color)
        if self.cursor:
            x, y, outline, white, black = self.cursor
            for dy in range(-4, 5):
                for dx in range(-4, 5):
                    d = dx*dx+dy*dy
                    inner, outer = 5 <= d <= 10, 10 < d <= 18
                    if not inner and not (outline and outer):
                        continue
                    px, py = x+dx, y+dy
                    if not (0 <= px < image.width and 0 <= py < image.height):
                        continue
                    if outline:
                        color = white if outer else black
                    else:
                        r, g, b = image.getpixel((px, py))
                        color = (black if (19595*r+38470*g+7471*b+0x8000) >> 16 >= 128 else white)
                    draw.point((px, py), color)
        return image
