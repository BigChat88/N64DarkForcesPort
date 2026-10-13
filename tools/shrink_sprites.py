#!/usr/bin/env python3
"""
shrink_sprites.py -- halve the resolution of the largest sprites in SPRITES.GOB.

A WAX sprite is loaded whole and stays in RAM while any object of its kind is in
the level. KELL.WAX (the Kell dragon, JABSHIP) is 585KB: with the rest of that
level the heap ends up so full and fragmented that a 46KB sound no longer finds a
hole. Its cells are stored at half the width and height here, and the world scale
of every animation is doubled, so the engine draws the sprite at the same size
(the loader multiplies cell sizes and frame offsets by the animation's
worldWidth / worldHeight, see TFE_Asset/spriteAsset_Jedi.cpp). Only the detail
changes: about a quarter of the pixels remain.

Each 2x2 block becomes one pixel: the top-left one if it is opaque, otherwise
the first opaque pixel of the block if at least two of the four are opaque
(thin details survive without the outline growing), otherwise transparent.

    python tools/shrink_sprites.py SPRITES.GOB OUT.GOB
    python tools/shrink_sprites.py --dir FOLDER     (shrinks FOLDER/SPRITES.GOB in place)
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

from compact_levels import read_gob, write_gob

# Sprites stored at half resolution.
SHRINK = {b"KELL.WAX"}

WAX_MAX_ANIM = 32
WAX_MAX_VIEWS = 32
WAX_MAX_FRAMES = 32
WAX_HEADER = 32 + WAX_MAX_ANIM * 4      # struct Wax
ANIM_SIZE = 28 + WAX_MAX_VIEWS * 4      # struct WaxAnim
VIEW_SIZE = 16 + WAX_MAX_FRAMES * 4     # struct WaxView
FRAME_SIZE = 32                         # struct WaxFrame
CELL_SIZE = 24                          # struct WaxCell
RUN_MAX = 0x7f


def _i32(data: bytes, pos: int) -> int:
    return struct.unpack_from("<i", data, pos)[0]


# ---------------------------------------------------------------------------
# Cells: column-major pixels, 0 = transparent
# ---------------------------------------------------------------------------
def decode_cell(data: bytes, pos: int) -> tuple:
    """(width, height, compressed, columns) with columns[x] = bytes of height pixels."""
    width, height, compressed = struct.unpack_from("<3i", data, pos)
    columns = []
    if compressed == 1:
        offsets = struct.unpack_from(f"<{width}I", data, pos + CELL_SIZE)
        for x in range(width):
            p = pos + offsets[x]
            col = bytearray()
            while len(col) < height:    # sprite_decompressColumn()
                count = data[p]
                p += 1
                if count & 0x80:
                    col += bytes(count & 0x7f)
                else:
                    col += data[p: p + count]
                    p += count
            columns.append(bytes(col[:height]))
    else:
        base = pos + CELL_SIZE
        columns = [data[base + x * height: base + (x + 1) * height] for x in range(width)]
    return width, height, compressed, columns


def encode_cell(width: int, height: int, compressed: int, columns: list) -> bytes:
    if compressed != 1:
        body = b"".join(columns)
        return struct.pack("<6i", width, height, compressed, CELL_SIZE + len(body), 0, 0) + body
    data = bytearray()
    offsets = []
    table = CELL_SIZE + width * 4
    for col in columns:
        offsets.append(table + len(data))
        y = 0
        while y < height:
            transparent = col[y] == 0
            run = y
            while run < height and run - y < RUN_MAX and (col[run] == 0) == transparent:
                run += 1
            if transparent:
                data.append(0x80 | (run - y))
            else:
                data.append(run - y)
                data += col[y:run]
            y = run
    head = struct.pack("<6i", width, height, compressed, table + len(data), 0, 0)
    return head + struct.pack(f"<{width}I", *offsets) + bytes(data)


def halve_columns(width: int, height: int, columns: list) -> tuple:
    new_w, new_h = (width + 1) // 2, (height + 1) // 2

    def px(x, y):
        return columns[x][y] if x < width and y < height else 0

    out = []
    for nx in range(new_w):
        col = bytearray(new_h)
        for ny in range(new_h):
            x, y = nx * 2, ny * 2
            block = (px(x, y), px(x + 1, y), px(x, y + 1), px(x + 1, y + 1))
            opaque = [c for c in block if c]
            if block[0]:
                col[ny] = block[0]
            elif len(opaque) >= 2:
                col[ny] = opaque[0]
        out.append(bytes(col))
    return new_w, new_h, out


# ---------------------------------------------------------------------------
# WAX
# ---------------------------------------------------------------------------
def shrink_wax(data: bytes) -> bytes:
    """Rebuilds the WAX with half-size cells; shared anims, views, frames and cells stay shared."""
    anim_offsets = [_i32(data, 32 + a * 4) for a in range(WAX_MAX_ANIM)]
    out = bytearray(data[:WAX_HEADER])
    placed = {}     # source offset -> new offset

    def place(src: int, body: bytes) -> int:
        # The engine reads the structures with 32-bit loads, which fault on the N64
        # unless they are 4-byte aligned (RLE cells have any length).
        out.extend(bytes(-len(out) % 4))
        placed[src] = len(out)
        out.extend(body)
        return placed[src]

    def put_cell(src: int) -> int:
        if src in placed:
            return placed[src]
        width, height, compressed, columns = decode_cell(data, src)
        width, height, columns = halve_columns(width, height, columns)
        return place(src, encode_cell(width, height, compressed, columns))

    def put_frame(src: int) -> int:
        if src in placed:
            return placed[src]
        offset_x, offset_y, flip, cell, w_ws, h_ws, pad1, pad2 = struct.unpack_from("<8i", data, src)
        new = place(src, bytes(FRAME_SIZE))
        cell = put_cell(cell) if cell else 0
        # Offsets are in cell pixels: halve them (round half away from zero).
        half = lambda v: -((-v + 1) // 2) if v < 0 else (v + 1) // 2
        struct.pack_into("<8i", out, new, half(offset_x), half(offset_y), flip, cell, w_ws, h_ws, pad1, pad2)
        return new

    def put_view(src: int) -> int:
        if src in placed:
            return placed[src]
        new = place(src, data[src: src + VIEW_SIZE])
        for f in range(WAX_MAX_FRAMES):
            frame = _i32(data, src + 16 + f * 4)
            if not frame:
                break
            struct.pack_into("<i", out, new + 16 + f * 4, put_frame(frame))
        return new

    def put_anim(src: int) -> int:
        if src in placed:
            return placed[src]
        new = place(src, data[src: src + ANIM_SIZE])
        world_w, world_h = struct.unpack_from("<2i", data, src)
        struct.pack_into("<2i", out, new, world_w * 2, world_h * 2)
        for v in range(WAX_MAX_VIEWS):
            view = _i32(data, src + 28 + v * 4)
            struct.pack_into("<i", out, new + 28 + v * 4, put_view(view) if view else 0)
        return new

    # The engine stops at the first empty animation: the ones after it are dropped.
    ended = False
    for a, src in enumerate(anim_offsets):
        ended = ended or not src
        struct.pack_into("<i", out, 32 + a * 4, 0 if ended else put_anim(src))
    return bytes(out)


def frame_world_boxes(data: bytes) -> list:
    """Every frame's box in world units (x0, y0, x1, y1), as the engine's loader computes it."""
    boxes = []
    world_w = world_h = None
    for a in range(WAX_MAX_ANIM):
        anim = _i32(data, 32 + a * 4)
        if not anim:
            break
        if world_w is None:
            world_w, world_h = struct.unpack_from("<2i", data, anim)
        for v in range(WAX_MAX_VIEWS):
            view = _i32(data, anim + 28 + v * 4)
            for f in range(WAX_MAX_FRAMES):
                frame = _i32(data, view + 16 + f * 4)
                if not frame:
                    break
                offset_x, offset_y, _, cell = struct.unpack_from("<4i", data, frame)
                width, height = struct.unpack_from("<2i", data, cell)
                x0 = -offset_x * world_w / 65536
                y0 = (height + offset_y) * world_h / 65536
                boxes.append((x0, y0, x0 + width * world_w / 65536, y0 - height * world_h / 65536))
    return boxes


def misaligned(data: bytes) -> list:
    """Offsets of the anims, views, frames and cells that are not 4-byte aligned."""
    found = set()
    for a in range(WAX_MAX_ANIM):
        anim = _i32(data, 32 + a * 4)
        if not anim:
            break
        found.add(anim)
        for v in range(WAX_MAX_VIEWS):
            view = _i32(data, anim + 28 + v * 4)
            found.add(view)
            for f in range(WAX_MAX_FRAMES):
                frame = _i32(data, view + 16 + f * 4)
                if not frame:
                    break
                found.update((frame, _i32(data, frame + 12)))
    return sorted(o for o in found if o % 4)


def check(original: bytes, small: bytes) -> float:
    """Largest difference between the frame boxes, in original pixels."""
    if misaligned(small):
        raise ValueError("misaligned structures")
    a, b = frame_world_boxes(original), frame_world_boxes(small)
    if len(a) != len(b):
        raise ValueError("frame count changed")
    scale = struct.unpack_from("<i", original, _i32(original, 32))[0] / 65536
    return max(abs(p - q) for box_a, box_b in zip(a, b) for p, q in zip(box_a, box_b)) / scale


def shrink_gob(data: bytes, log=print) -> bytes:
    result = []
    for name, body in read_gob(data):
        if name.upper() in SHRINK:
            small = shrink_wax(body)
            error = check(body, small)
            if error > 2.0:     # one pixel at the new resolution
                log(f"  {name.decode('latin1')}: kept as is (frames would move by {error:.1f} pixels)")
            else:
                log(f"  {name.decode('latin1')}: {len(body) // 1024}KB -> {len(small) // 1024}KB (half resolution)")
                body = small
        result.append((name, body))
    return write_gob(result)


def main() -> None:
    ap = argparse.ArgumentParser(description="Halve the resolution of the largest sprites in SPRITES.GOB.")
    ap.add_argument("src", nargs="?", type=Path, help="SPRITES.GOB to read")
    ap.add_argument("dst", nargs="?", type=Path, help="GOB to write")
    ap.add_argument("--dir", type=Path, help="shrink the SPRITES.GOB found in this folder (any letter case) in place")
    args = ap.parse_args()
    if args.dir:
        found = [p for p in args.dir.iterdir() if p.name.upper() == "SPRITES.GOB"]
        if not found:
            print(f"  no SPRITES.GOB in {args.dir}, nothing to shrink")
            return
        args.src = args.dst = found[0]
    elif not (args.src and args.dst):
        ap.error("give SRC and DST, or --dir")
    data = shrink_gob(args.src.read_bytes())
    args.dst.write_bytes(data)


if __name__ == "__main__":
    main()
