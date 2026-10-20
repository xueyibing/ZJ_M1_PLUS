"""Extract RGB565 pixel arrays from ui/egg_img_*.c into PNG files."""
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI_DIR = ROOT / "ui"
OUT_DIR = ROOT / "tmp" / "egg_imgs"
OUT_DIR.mkdir(parents=True, exist_ok=True)

W, H = 480, 320

HEX_RE = re.compile(r"0x([0-9a-fA-F]{2})")


def extract_bytes(c_file: Path) -> bytes:
    text = c_file.read_text(encoding="utf-8", errors="ignore")
    m = re.search(r"_map\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise RuntimeError(f"array not found in {c_file.name}")
    return bytes(int(b, 16) for b in HEX_RE.findall(m.group(1)))


def rgb565_to_rgb888(data: bytes, big_endian: bool) -> bytes:
    out = bytearray(W * H * 3)
    for i in range(W * H):
        lo, hi = data[2 * i], data[2 * i + 1]
        px = (lo << 8) | hi if big_endian else (hi << 8) | lo
        r = ((px >> 11) & 0x1F) * 255 // 31
        g = ((px >> 5) & 0x3F) * 255 // 63
        b = (px & 0x1F) * 255 // 31
        out[3 * i:3 * i + 3] = bytes((r, g, b))
    return bytes(out)


def write_png(path: Path, rgb: bytes) -> None:
    raw = b"".join(b"\x00" + rgb[y * W * 3:(y + 1) * W * 3] for y in range(H))

    def chunk(tag: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    path.write_bytes(png)


for name in ("easy", "sunny", "well"):
    data = extract_bytes(UI_DIR / f"egg_img_{name}.c")
    if len(data) != W * H * 2:
        print(f"egg_img_{name}: unexpected {len(data)} bytes", file=sys.stderr)
        continue
    for be in (True, False):
        suffix = "be" if be else "le"
        out = OUT_DIR / f"egg_img_{name}_{suffix}.png"
        write_png(out, rgb565_to_rgb888(data, be))
        print(f"OK {out.name}")
