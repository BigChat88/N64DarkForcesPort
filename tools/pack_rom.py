#!/usr/bin/env python3
"""
pack_rom.py -- turn your own copy of Dark Forces into a Nintendo 64 ROM,
without Docker or the N64 toolchain.

It does everything the Makefile's ROM rule does *except* compile the engine.
The compiled engine (darkforces64.elf.stripped + .sym + intro sprites) is the
same for everyone - grab it from a GitHub Release (it ships in
tools/vendor/engine/), or build it yourself with `make engine` inside
`libdragon exec` (it lands in build/engine/, which is also searched).

    python tools/pack_rom.py
    python tools/pack_rom.py --gamedata "C:/Games/Dark Forces/Game"
    python tools/pack_rom.py --gamedata star-wars-dark-forces.zip

--gamedata may be a folder (your install folder works as-is) or a .zip; the
files below are looked up anywhere inside it, in any letter case:

    DARK.GOB  SOUNDS.GOB  SPRITES.GOB  TEXTURES.GOB  LOCAL.MSG  LFD/*.LFD

Requires the libdragon host tools (mkdfs, n64tool, ed64romconfig, and
audioconv64 for the music) -- found via --tools-dir, $N64_INST/bin,
tools/vendor/bin/ or PATH. See tools/vendor/bin/README.md.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from compact_levels import compact_gob

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

ROM_NAME = "darkforces64"
REQUIRED_FILES = ("DARK.GOB", "SOUNDS.GOB", "SPRITES.GOB", "TEXTURES.GOB", "LOCAL.MSG")
LFD_DIR = "LFD"
MUSIC_NAME = "MUSIC.SF64"
# The project's General MIDI SoundFont, converted to MUSIC.SF64 (see soundfont/README.md).
SOUNDFONT = ROOT / "soundfont" / "SC55.sf2"

ENGINE_FILES = (f"{ROM_NAME}.elf.stripped", f"{ROM_NAME}.elf.sym")
# Fallback header settings if the engine folder has no rom.cfg (written by
# `make engine`, which takes them from the Makefile).
DEFAULT_ROM_CFG = {"title": "Dark Forces 64", "savetype": "sram256k",
                   "expansionpak": "required", "regionfree": "1"}


def project_version() -> str:
    try:
        return (ROOT / "VERSION").read_text(encoding="utf-8").strip() or "0.0.0"
    except OSError:
        return "0.0.0"


def log(msg: str) -> None:
    print(msg, flush=True)


def die(msg: str):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def run(cmd: list, **kw) -> None:
    log("  $ " + " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def exe(name: str) -> str:
    return name + (".exe" if os.name == "nt" else "")


def find_tool(name: str, extra_dir: Path | None) -> Path:
    cands: list = []
    if extra_dir:
        cands.append(extra_dir / exe(name))
    n64_inst = os.environ.get("N64_INST")
    if n64_inst:
        cands.append(Path(n64_inst) / "bin" / exe(name))
    cands.append(HERE / "vendor" / "bin" / exe(name))
    which = shutil.which(name)
    if which:
        cands.append(Path(which))
    for c in cands:
        if c.is_file():
            return c
    die(
        f"host tool '{name}' not found.\n"
        f"  looked in: {', '.join(str(c) for c in cands)}\n"
        f"  download a Release, build it with tools/vendor/bin/build-tools.sh,\n"
        f"  install the libdragon toolchain and set N64_INST, or pass --tools-dir"
    )


def find_engine(engine_dir: Path | None) -> Path:
    dirs = [engine_dir] if engine_dir else [HERE / "vendor" / "engine", ROOT / "build" / "engine"]
    for d in dirs:
        if all((d / f).is_file() for f in ENGINE_FILES):
            return d
    die(
        f"compiled engine not found ({' + '.join(ENGINE_FILES)}).\n"
        f"  looked in: {', '.join(str(d) for d in dirs)}\n"
        "  download a Release, build it with `make engine` inside\n"
        "  `libdragon exec`, or pass --engine-dir. See tools/vendor/engine/README.md"
    )


def read_rom_cfg(engine: Path) -> dict:
    cfg = dict(DEFAULT_ROM_CFG)
    f = engine / "rom.cfg"
    if f.is_file():
        for line in f.read_text(encoding="utf-8").splitlines():
            key, sep, value = line.partition("=")
            if sep:
                cfg[key.strip()] = value.strip()
    return cfg


class GameData:
    """Case-insensitive view of the Dark Forces files in a folder or a .zip."""

    def __init__(self, path: Path):
        self.path = path
        self.zip = None
        # upper-case "NAME" / "LFD/NAME" -> source (Path, or name inside the zip)
        self.files: dict = {}
        if path.is_file() and path.suffix.lower() == ".zip":
            self.zip = zipfile.ZipFile(path)
            entries = [(n, n) for n in self.zip.namelist() if not n.endswith("/")]
        elif path.is_dir():
            entries = [(str(p.relative_to(path)).replace("\\", "/"), p)
                       for p in path.rglob("*") if p.is_file()]
        else:
            die(f"{path}: not a folder or a .zip")
        # Shallowest match wins, so a stray copy in a subfolder doesn't.
        for rel, src in sorted(entries, key=lambda e: e[0].count("/")):
            parts = rel.upper().split("/")
            name = parts[-1]
            key = f"{LFD_DIR}/{name}" if name.endswith(".LFD") else name
            self.files.setdefault(key, src)

    def has(self, key: str) -> bool:
        return key in self.files

    def lfds(self) -> list:
        return sorted(k for k in self.files if k.startswith(LFD_DIR + "/"))

    def copy(self, key: str, dest: Path) -> None:
        dest.parent.mkdir(parents=True, exist_ok=True)
        src = self.files[key]
        if self.zip:
            with self.zip.open(src) as fin, open(dest, "wb") as fout:
                shutil.copyfileobj(fin, fout)
        else:
            shutil.copy2(src, dest)


def find_gamedata(explicit: Path | None) -> GameData:
    if explicit:
        return GameData(explicit)
    default = ROOT / "gamedata"
    data = GameData(default)
    if data.has("DARK.GOB"):
        return data
    zips = sorted(p for p in default.glob("*") if p.suffix.lower() == ".zip")
    if len(zips) == 1:
        return GameData(zips[0])
    return data


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Pack your own Dark Forces files + the prebuilt engine into a Nintendo 64 ROM.")
    ap.add_argument("--version", action="version", version=f"N64DarkForcesPort {project_version()}")
    ap.add_argument("--gamedata", type=Path, default=None,
                    help="folder or .zip with your Dark Forces files (default: gamedata/, "
                         "or the single .zip inside it)")
    ap.add_argument("--out", type=Path, default=None,
                    help=f"output ROM path (default: output/{ROM_NAME}.z64)")
    ap.add_argument("--engine-dir", type=Path, default=None,
                    help="folder with the prebuilt engine (default: tools/vendor/engine, "
                         "then build/engine)")
    ap.add_argument("--tools-dir", type=Path, default=None,
                    help="folder with the libdragon host tools "
                         "(default: $N64_INST/bin, tools/vendor/bin, PATH)")
    ap.add_argument("--keep-work", action="store_true", help="keep the temporary work folder")
    args = ap.parse_args()

    log(f"N64DarkForcesPort {project_version()}")

    data = find_gamedata(args.gamedata)
    missing = [f for f in REQUIRED_FILES if not data.has(f)]
    if not data.lfds():
        missing.append(f"{LFD_DIR}/*.LFD")
    if missing:
        die(
            f"{data.path}: missing Dark Forces files: {', '.join(missing)}\n"
            "  copy them from your own copy of the game (see gamedata/README.md),\n"
            "  or point --gamedata at your install folder or a .zip of it"
        )

    if not SOUNDFONT.is_file():
        die(f"{SOUNDFONT}: missing - it is part of the project, get it again from the repository")
    engine = find_engine(args.engine_dir)
    cfg = read_rom_cfg(engine)
    tool_names = ["mkdfs", "n64tool", "ed64romconfig", "audioconv64"]
    tools = {t: find_tool(t, args.tools_dir) for t in tool_names}
    out = args.out or (ROOT / "output" / f"{ROM_NAME}.z64")

    log(f"gamedata : {data.path}  ({len(data.lfds())} LFD file(s))")
    log(f"music    : {SOUNDFONT}")
    log(f"engine   : {engine}")
    for t, p in tools.items():
        log(f"tool     : {t:14s} {p}")

    work = Path(tempfile.mkdtemp(prefix="dfpack-"))
    fsroot = work / "filesystem"
    fsroot.mkdir()
    try:
        log("[1/4] staging filesystem")
        for key in list(REQUIRED_FILES) + data.lfds():
            data.copy(key, fsroot / key)
        # Without the indentation and comments of the level files the ROM fits in 64MB.
        gob = fsroot / "DARK.GOB"
        gob.write_bytes(compact_gob(gob.read_bytes(), log=log))
        intro = engine / "intro"
        if intro.is_dir():
            shutil.copytree(intro, fsroot / "intro")

        log("[2/4] music")
        sf_out = work / "sf64"
        sf_out.mkdir()
        # Run from the SoundFont's folder with a bare file name: audioconv64
        # only splits paths on '/', so a Windows path would end up in the
        # output file name.
        run([tools["audioconv64"], "-o", sf_out, SOUNDFONT.name], cwd=str(SOUNDFONT.parent))
        converted = sorted(sf_out.glob("*.sf64"))
        if not converted:
            die(f"audioconv64 produced no .sf64 from {SOUNDFONT}")
        shutil.move(str(converted[0]), fsroot / MUSIC_NAME)

        log("[3/4] mkdfs")
        dfs = work / f"{ROM_NAME}.dfs"
        run([tools["mkdfs"], dfs, fsroot])

        log("[4/4] n64tool + ed64romconfig")
        rom = work / f"{ROM_NAME}.z64"
        extra = sorted(engine.glob("*.version"))
        run([tools["n64tool"], "--toc", "--title", cfg["title"], "--output", rom,
             "--align", "256", engine / ENGINE_FILES[0], engine / ENGINE_FILES[1], dfs, *extra])
        run([tools["ed64romconfig"], "--savetype", cfg["savetype"],
             "--expansionpak", cfg["expansionpak"],
             *(["--regionfree"] if cfg["regionfree"] == "1" else []), rom])

        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(rom), out)
        log(f"\nOK -> {out}  ({out.stat().st_size / 1024 / 1024:.1f} MiB)")
    finally:
        if data.zip:
            data.zip.close()
        if args.keep_work:
            log(f"work folder kept: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
