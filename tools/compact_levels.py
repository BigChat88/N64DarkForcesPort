#!/usr/bin/env python3
"""
compact_levels.py -- shrink the level text files inside DARK.GOB.

The level geometry (.LEV) and objects (.O) are text exported from AutoCAD,
full of indentation, alignment spaces and comments: about 4MB of DARK.GOB. The
engine's parser skips all of that, so it is removed when the ROM is packed,
which keeps the ROM under 64MB. The data the engine reads does not change:

* Runs of spaces and tabs become one space (sscanf() treats any amount of
  whitespace the same), a line keeps a single leading space if it had any, and
  trailing spaces and empty lines go.
* '#' comments are removed, except on the line read right after "SECTOR n":
  the engine reads the sector name with comments only at the beginning of the
  line, so a name may contain '#'.
* In .O files, lines that are a whole /* ... */ comment are removed. '#' is not
  stripped from a line that opens or closes a block comment.
* .INF files are left alone: their parser does not treat '#' as a comment.

Every compacted file is checked against the original with a port of the
engine's TFE_Parser::readLine(): if the lines the engine would read differ in
any way, the original file is kept.

    python tools/compact_levels.py DARK.GOB OUT.GOB
    python tools/compact_levels.py --dir FOLDER     (compacts FOLDER/DARK.GOB in place)
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

GOB_MAGIC = b"GOB\x0a"
GOB_NAME_LEN = 13

# Parser settings of each file type, as in TFE_Jedi/Level/level.cpp.
LEV_SETTINGS = {"comments": (b"#",), "block": False}
O_SETTINGS = {"comments": (b"//", b"#"), "block": True}


# ---------------------------------------------------------------------------
# GOB archives
# ---------------------------------------------------------------------------
def read_gob(data: bytes) -> list:
    """[(name, bytes)] in archive order."""
    if data[:4] != GOB_MAGIC:
        raise ValueError("not a GOB archive")
    index = struct.unpack_from("<I", data, 4)[0]
    count = struct.unpack_from("<I", data, index)[0]
    entries = []
    for i in range(count):
        pos = index + 4 + i * (8 + GOB_NAME_LEN)
        offset, size = struct.unpack_from("<II", data, pos)
        name = data[pos + 8: pos + 8 + GOB_NAME_LEN].split(b"\0")[0]
        entries.append((name, data[offset: offset + size]))
    return entries


def write_gob(entries: list) -> bytes:
    out = bytearray(GOB_MAGIC + b"\0\0\0\0")
    offsets = []
    for _, body in entries:
        offsets.append(len(out))
        out += body
    struct.pack_into("<I", out, 4, len(out))
    out += struct.pack("<I", len(entries))
    for (name, body), offset in zip(entries, offsets):
        out += struct.pack("<II", offset, len(body)) + name.ljust(GOB_NAME_LEN, b"\0")
    return bytes(out)


# ---------------------------------------------------------------------------
# Port of TFE_Parser::readLine() (tfe/TFE_System/parser.cpp)
# ---------------------------------------------------------------------------
def _is_whitespace(c: int) -> bool:
    # 'char' is signed in the engine: bytes >= 128 count as whitespace too.
    return not (32 < c < 127)


class ParserPort:
    def __init__(self, data: bytes, settings: dict):
        self.data = data
        self.comments = settings["comments"]
        self.block = settings["block"]
        self.in_block = False
        self.pos = 0
        # Without block comments a line never depends on the previous ones: split it up front.
        self.lines = None if self.block else re.split(rb"[\r\n]+", data)
        self.line_index = 0

    def _at(self, i: int) -> int:
        return self.data[i] if i < len(self.data) else 0

    def _is_comment(self, i: int) -> bool:
        return any(self.data.startswith(c, i) for c in self.comments)

    def _has_content(self, line: bytes, comment_only_at_beginning: bool) -> bool:
        for k, c in enumerate(line):
            if not _is_whitespace(c):
                if comment_only_at_beginning and any(line[k:].startswith(cm) for cm in self.comments):
                    return False
                return True
        return False

    def _upper(self, line: bytes) -> bytes:
        return line.upper()     # toupper() in the C locale only changes a-z

    def read_line(self, comment_only_at_beginning: bool = False):
        if self.lines is not None:
            while self.line_index < len(self.lines):
                line = self.lines[self.line_index]
                self.line_index += 1
                if not comment_only_at_beginning:
                    cut = [line.find(c) for c in self.comments if c in line]
                    if cut:
                        line = line[:min(cut)]
                if self._has_content(line, comment_only_at_beginning):
                    return self._upper(line)
            return None

        data, n = self.data, len(self.data)
        if self.pos >= n:
            return None
        line = bytearray()
        has_content = False
        while not has_content and self.pos < n:
            line = bytearray()
            in_comment = False
            i = self.pos
            while i < n:
                self.pos = i + 1
                c = data[i]
                if i > 0 and data[i - 1] == 0x2A and c == 0x2F:          # "*/"
                    self.in_block = False
                elif c == 0x2F and self._at(i + 1) == 0x2A:              # "/*"
                    self.in_block = True
                elif c in (0x0A, 0x0D):
                    for j in range(i + 1, n):
                        if data[j] not in (0x0A, 0x0D):
                            self.pos = j
                            break
                    break
                elif not in_comment and not self.in_block:
                    if not comment_only_at_beginning:
                        in_comment = self._is_comment(i)
                    if not in_comment:
                        line.append(c)
                i += 1
            has_content = self._has_content(bytes(line), comment_only_at_beginning)
        return self._upper(bytes(line)) if line else None


def _normalize(line: bytes) -> bytes:
    """What sscanf() can tell apart: one space for any run of spaces and tabs, none at the end."""
    return re.sub(rb"[ \t]+", b" ", line).rstrip(b" ")


def _reads(data: bytes, settings: dict) -> list:
    """Every line the engine would read, including the sector name read after each SECTOR line."""
    parser = ParserPort(data, settings)
    lines = []
    while True:
        line = parser.read_line()
        if line is None:
            break
        lines.append(_normalize(line))
        if line.lstrip().startswith(b"SECTOR "):
            name = parser.read_line(comment_only_at_beginning=True)
            if name is None:
                break
            lines.append(b"NAME-LINE " + _normalize(name))
    return lines


# ---------------------------------------------------------------------------
# Compaction
# ---------------------------------------------------------------------------
def _compact(data: bytes, is_o: bool) -> bytes:
    out = []
    keep_hash = False       # next content line is the sector name
    for raw in data.split(b"\n"):
        line = raw.replace(b"\r", b"").replace(b"\t", b" ")
        stripped = line.strip(b" ")
        if is_o and stripped.startswith(b"/*") and stripped.endswith(b"*/") and stripped.count(b"*/") == 1:
            continue
        if stripped.startswith(b"#") and b"/*" not in line and b"*/" not in line:
            continue            # comment-only line: no content in any parser mode
        if not keep_hash and b"/*" not in line and b"*/" not in line:
            line = line.split(b"#")[0]
        body = re.sub(rb" +", b" ", line).strip(b" ")
        if not body:
            continue
        lead = b" " if line[:1] == b" " else b""
        out.append(lead + body)
        if keep_hash:
            keep_hash = False
        elif body.upper().startswith(b"SECTOR "):
            keep_hash = True
    return b"\n".join(out) + b"\n"


def compact_gob(data: bytes, log=print) -> bytes:
    entries = read_gob(data)
    result, saved, kept = [], 0, []
    for name, body in entries:
        upper = name.upper()
        settings = LEV_SETTINGS if upper.endswith(b".LEV") else O_SETTINGS if upper.endswith(b".O") else None
        if settings is None:
            result.append((name, body))
            continue
        small = _compact(body, upper.endswith(b".O"))
        if _reads(small, settings) == _reads(body, settings):
            result.append((name, small))
            saved += len(body) - len(small)
        else:
            result.append((name, body))
            kept.append(name.decode("latin1"))
    for name in kept:
        log(f"  {name}: kept as is (compacted copy would read differently)")
    log(f"  level files: {saved / 1048576:.2f} MiB smaller")
    return write_gob(result)


def main() -> None:
    ap = argparse.ArgumentParser(description="Remove indentation and comments from the level files in DARK.GOB.")
    ap.add_argument("src", nargs="?", type=Path, help="DARK.GOB to read")
    ap.add_argument("dst", nargs="?", type=Path, help="compacted GOB to write")
    ap.add_argument("--dir", type=Path, help="compact the DARK.GOB found in this folder (any letter case) in place")
    args = ap.parse_args()
    if args.dir:
        found = [p for p in args.dir.iterdir() if p.name.upper() == "DARK.GOB"]
        if not found:
            print(f"  no DARK.GOB in {args.dir}, nothing to compact")
            return
        args.src = args.dst = found[0]
    elif not (args.src and args.dst):
        ap.error("give SRC and DST, or --dir")
    data = compact_gob(args.src.read_bytes())
    args.dst.write_bytes(data)


if __name__ == "__main__":
    main()
