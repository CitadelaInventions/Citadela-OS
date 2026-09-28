"""Content-addressed BMP files and local rendering of kernel bitmap properties."""

import io
import struct
from pathlib import Path
from PIL import Image


def fnv1a(data):
    result = 2166136261
    for value in data:
        result = ((result ^ value) * 16777619) & 0xffffffff
    return result


class Assets:
    def __init__(self, directory=None):
        self.directory = Path(directory) if directory else None
        self.files = {}
        self.pending = {}
        self.rendered = {}

    def begin(self, identity, size):
        if not 54 <= size <= 8*1024*1024:
            raise ValueError("Unsupported BMP file size")
        if identity not in self.files and self.directory:
            path = self.directory / f"{identity:08X}.bmp"
            if path.exists():
                data = path.read_bytes()
                if len(data) == size and fnv1a(data) == identity:
                    self._decode(identity, data)
        self.pending[identity] = (size, bytearray())
        return identity in self.files

    def chunk(self, identity, offset, data):
        if identity not in self.pending:
            raise ValueError("No active asset transfer")
        size, buffer = self.pending[identity]
        if offset != len(buffer) or len(buffer)+len(data) > size:
            raise ValueError("Invalid BMP file offset")
        buffer.extend(data)

    def finish(self, identity):
        size, data = self.pending[identity]
        if len(data) != size or fnv1a(data) != identity:
            raise ValueError("BMP file checksum mismatch")
        self._decode(identity, data)
        if self.directory:
            self.directory.mkdir(parents=True, exist_ok=True)
            temporary = self.directory / f"{identity:08X}.part"
            temporary.write_bytes(data)
            temporary.replace(self.directory / f"{identity:08X}.bmp")
        del self.pending[identity]

    def _decode(self, identity, data):
        with Image.open(io.BytesIO(data)) as image:
            if image.format != "BMP" or image.width*image.height > 4*1024*1024:
                raise ValueError("Invalid BMP asset")
            self.files[identity] = image.convert("RGB")
            self.files[identity].info["top_down"] = struct.unpack_from("<i", data, 22)[0] < 0

    def bitmap(self, a, tables, palette):
        (identity, mode, ox, oy, w, h, cx, cy, cw, ch, brightness, mono, red,
         skip_white, skip_black, wt, bt, wb, bb, aw, ab, colour) = a
        if identity not in self.files:
            raise ValueError("BMP asset has not been transferred")
        tone = tables[2 if mode == 2 else 1]
        key = (identity, mode, w, h, brightness, mono, red, skip_white, skip_black,
               wt, bt, wb, bb, aw, ab, colour, bytes(tone), bytes(tables[3]),
               bytes(tables[4]), bytes(tables[5]), tuple(palette))
        if key not in self.rendered:
            source = self.files[identity]
            pixels = []
            data = source.get_flattened_data() if hasattr(source, "get_flattened_data") else source.getdata()
            for rr, gg, b in data:
                if mode == 1:
                    lum = min(255, (299*rr+587*gg+114*b)*brightness/100000)
                    r, g, b = (min(255, max(0, int(v*brightness/100+.5))) for v in (rr, b, b))
                else:
                    r, g, b = tone[rr], tone[gg], tone[b]
                    lum = ((299*r+587*g+114*b)//1000 if mode == 0
                           else .299*r+.587*g+.114*b)
                transparent = ((skip_white and aw <= 255 and lum >= aw) or
                               (skip_black and lum <= ab))
                if mode == 0:
                    if wb > 0 and lum >= wt:
                        total = r+g+b+1
                        r, g, b = (((v*(256-wb)+(int(lum)*v//total)*wb)+128) >> 8 for v in (r, g, b))
                    if bb > 0 and lum <= bt:
                        r, g, b = ((v*(256-bb)+128) >> 8 for v in (r, g, b))
                    if mono:
                        r = int(lum) if red else 0
                        g = b = int(lum)
                    else:
                        g = b
                else:
                    g = b
                if not red:
                    r = 0
                if not colour:
                    r = g = b = (19595*r+38470*g+7471*b+0x8000) >> 16
                r, g, b = tables[3][r], tables[4][g], tables[5][b]
                if max(r, g, b)-min(r, g, b) <= 6:
                    gray = (19595*r+38470*g+7471*b+0x8000) >> 16
                    index = (gray*31+127)//255
                else:
                    index = 32+(((r*6+127)//255*8+(g*7+127)//255)*4+(b*3+127)//255)
                pixels.append((*palette[index], 0 if transparent else 255))
            transformed = Image.new("RGBA", source.size)
            transformed.putdata(pixels)
            original = transformed.load()
            # Match the driver's integer X mapping and row replication boundaries.
            ymap = [min(source.height-1, ((y+1)*source.height-1)//h) if mode == 0 and source.info.get("top_down")
                    else min(source.height-1, y*source.height//h) for y in range(h)]
            result = Image.new("RGBA", (w, h))
            result.putdata([original[x*source.width//w, ymap[y]] for y in range(h) for x in range(w)])
            if len(self.rendered) >= 24:
                self.rendered.clear()
            self.rendered[key] = result
        image = self.rendered[key]
        left, top = max(0, cx), max(0, cy)
        right, bottom = min(w, cx+cw), min(h, cy+ch)
        return image.crop((left, top, max(left, right), max(top, bottom))), (ox+left, oy+top)
