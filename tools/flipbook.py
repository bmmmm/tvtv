#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render whole scenes with build/preview and bundle the frames into one self-contained HTML
flipbook: playable, scrubbable, with a timeline and notes per scene. Standard library only,
and the page loads nothing from the net.

Usage (after `make preview`, from any directory):
    python3 tools/flipbook.py <spec.json> <out.html>

The spec names the scenes, frame rates, timeline marks and notes; tools/flipbook.{en,de}.json
are the two languages of the site. Frames go to build/flipbook/<scene>/ as BMP and are embedded
as PNG data URIs; the page shares web/style.css with the site, inlined.
"""
import base64
import html
import json
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PREVIEW = ROOT / "build" / "preview"
CSS = ROOT / "web" / "style.css"
FRAMES = ROOT / "build" / "flipbook"
# Sample 1 ms after each frame time: a scene stepped at the same rate (crt at 12.5 fps) must
# not be sampled exactly on its step boundaries, where float rounding picks the previous step.
EPS = 0.001
NAME = re.compile(r"^[a-z0-9_]+$")


def bmp_to_png(data):
    """24-bit bottom-up BMP (as written by tools/preview.c) -> PNG bytes."""
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp != 24 or h <= 0:
        raise ValueError("expected a 24-bit bottom-up BMP")
    stride = (w * 3 + 3) & ~3
    raw = bytearray()
    for y in range(h - 1, -1, -1):
        line = data[off + y * stride : off + y * stride + w * 3]
        px = bytearray(line)
        px[0::3], px[2::3] = line[2::3], line[0::3]  # BGR -> RGB
        raw.append(0)  # filter type: none
        raw += px

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def render(seq):
    scene = seq["scene"]
    if not NAME.match(scene):
        sys.exit(f"bad scene name: {scene!r}")
    d = FRAMES / scene
    d.mkdir(parents=True, exist_ok=True)
    for old in d.glob("*.bmp"):
        old.unlink()
    t0, t1, fps = seq.get("t0", 0), seq["t1"], seq["fps"]
    subprocess.run([str(PREVIEW), "-1x", scene, "seq", str(d), str(t0 + EPS), str(t1 + EPS), str(fps)],
                   check=True, stdout=subprocess.DEVNULL, cwd=ROOT)
    frames = ["data:image/png;base64," + base64.b64encode(bmp_to_png(p.read_bytes())).decode()
              for p in sorted(d.glob("*.bmp"))]
    if not frames:
        sys.exit(f"{scene}: no frames, check t0, t1 and fps")
    return frames


def esc(s):
    return html.escape(str(s), quote=True)


def section(seq, n):
    sid = seq["scene"]
    dur = seq["t1"] - seq.get("t0", 0)
    notes = "".join(f"<dt>{esc(k)}</dt><dd>{esc(v)}</dd>" for k, v in seq.get("notes", []))
    return f"""
  <section class="set" aria-labelledby="h-{sid}">
    <div class="set-head">
      <h2 id="h-{sid}">{esc(seq.get("title", sid))}</h2>
      <span class="tag">{esc(sid)} · {dur:g} s · {n} frames</span>
    </div>
    <div class="tv"><canvas id="{sid}-canvas" width="360" height="288" aria-label="scene {esc(sid)}"></canvas></div>
    <div class="console">
      <button id="{sid}-play" type="button">Pause</button>
      <input id="{sid}-scrub" type="range" min="0" max="{n - 1}" value="0" aria-label="frame">
      <select id="{sid}-speed" aria-label="speed">
        <option value="1">1×</option><option value="0.5">½×</option><option value="0.25">¼×</option>
      </select>
      <span id="{sid}-readout" class="readout"></span>
    </div>
    <div id="{sid}-timeline" class="timeline" aria-hidden="true"></div>
    {f'<div class="facts"><dl>{notes}</dl></div>' if notes else ''}
  </section>"""


def topbar(spec):
    nav = "".join(f'<a href="{esc(n["href"])}"{" aria-current=\"page\"" if n.get("current") else ""}>{esc(n["label"])}</a>'
                  for n in spec.get("nav", []))
    alt = spec.get("alt")
    lang = (f'<span class="lang"><a href="{esc(alt["href"])}" hreflang="{esc(alt["lang"])}" lang="{esc(alt["lang"])}">'
            f'{esc(alt["label"])}</a></span>' if alt else "")
    return f'<div class="topbar"><nav aria-label="site">{nav}</nav>{lang}</div>' if nav or lang else ""


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: tools/flipbook.py <spec.json> <out.html>")
    if not PREVIEW.exists():
        sys.exit(f"{PREVIEW} missing: run `make preview` first")
    spec = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    tpl = Path(__file__).with_name("flipbook.tpl.html").read_text(encoding="utf-8")
    sections, seqs, seen = [], [], set()
    for seq in spec["sequences"]:
        if seq["scene"] in seen:
            sys.exit(f"scene listed twice: {seq['scene']}")
        seen.add(seq["scene"])
        frames = render(seq)
        sections.append(section(seq, len(frames)))
        seqs.append({"id": seq["scene"], "fps": seq["fps"], "t0": seq.get("t0", 0),
                     "marks": seq.get("marks", []), "frames": frames})
        print(f"{seq['scene']}: {len(frames)} frames", file=sys.stderr)
    fills = {
        "LANG": esc(spec.get("lang", "en")),
        "TITLE": esc(spec["title"]),
        "EYEBROW": esc(spec.get("eyebrow", "")),
        "LEDE": esc(spec.get("lede", "")),
        "FOOTER": esc(spec.get("footer", "")),
        "CREDITS": esc(spec.get("credits", "")),
        "TOPBAR": topbar(spec),
        "CSS": CSS.read_text(encoding="utf-8"),
        "SECTIONS": "".join(sections),
        "SEQS": json.dumps(seqs).replace("</", "<\\/"),  # keep a label from closing the <script>
    }
    # one pass, so that spec text containing a placeholder is never substituted again
    out = re.sub(r"\{\{(LANG|TITLE|EYEBROW|LEDE|FOOTER|CREDITS|TOPBAR|CSS|SECTIONS)\}\}|/\*SEQS\*/",
                 lambda m: fills[m.group(1) or "SEQS"], tpl)
    Path(sys.argv[2]).write_text(out, encoding="utf-8")
    print(f"{sys.argv[2]}: {len(out.encode()) / 1e6:.1f} MB", file=sys.stderr)


if __name__ == "__main__":
    main()
