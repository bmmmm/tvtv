#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render the whitepaper's figures with build/preview: one PNG per (scene, time), 720x576 as the
TV shows them. Every scene is played from 0 to its time so that state built frame by frame
(stars, flowers, explode, matrix) is right. The scene figures are generated here at build time;
the separate social avatar concepts in docs/img are documentation assets.

Usage (after `make preview`):  python3 tools/figures.py <outdir>
"""
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from flipbook import EPS, PREVIEW, ROOT, bmp_to_png  # noqa: E402

FIGURES = [  # scene, time in seconds
    ("testcard", 1.0),
    ("crt", 9.5),
    ("plasma", 3.0),
    ("fire", 4.0),
    ("explode", 1.0),
    ("juggle", 3.0),
    ("bernd", 10.0),
    ("outrun", 4.0),
    ("explain", 1.0),
    ("stack", 61.0),
]


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: tools/figures.py <outdir>")
    if not PREVIEW.exists():
        sys.exit(f"{PREVIEW} missing: run `make preview` first")
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for scene, t in FIGURES:
        d = ROOT / "build" / "figures" / scene
        d.mkdir(parents=True, exist_ok=True)
        for old in d.glob("*.bmp"):
            old.unlink()
        subprocess.run([str(PREVIEW), scene, "seq", str(d), str(EPS), str(t + EPS), "50"],
                       check=True, stdout=subprocess.DEVNULL, cwd=ROOT)
        last = sorted(d.glob("*.bmp"))[-1]
        (out / f"{scene}.png").write_bytes(bmp_to_png(last.read_bytes()))
        print(f"{scene}.png: t = {t:g} s", file=sys.stderr)


if __name__ == "__main__":
    main()
