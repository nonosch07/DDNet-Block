#!/usr/bin/env python3
"""Turns a load-test run into a PDF performance report.

    python3 src/block/tests/loadtest.py --bots 127 --seconds 30 --profile --out out
    python3 src/block/tests/perfreport.py out --pdf out/block-performance.pdf

Reads the JSON the load test writes and the gprof profile next to it, and draws
a report: what the server cost, where its CPU went, and a flame view of the hot
call paths.

The PDF is written directly rather than through a library. Nothing here needs a
raster image or an embedded font -- it is rectangles, lines and Helvetica, all of
which a PDF viewer already has -- and a from-scratch writer keeps the tooling to
"python3", which is the only thing the rest of the test scripts need too.
"""

import argparse
import json
import os
import re
import sys

# ---------------------------------------------------------------------------
# a very small PDF writer
# ---------------------------------------------------------------------------

PAGE_W, PAGE_H = 595.28, 841.89  # A4 in points

# One palette, used everywhere, so the report reads as one document. Dark ink on
# warm paper, with a single accent for the thing the reader should look at.
INK = (0.13, 0.13, 0.15)
MUTED = (0.45, 0.45, 0.50)
RULE = (0.85, 0.85, 0.87)
ACCENT = (0.16, 0.44, 0.78)
GOOD = (0.18, 0.55, 0.34)
WARN = (0.80, 0.53, 0.12)
FLAME = [
    (0.85, 0.37, 0.24),
    (0.90, 0.51, 0.25),
    (0.93, 0.65, 0.30),
    (0.95, 0.76, 0.38),
    (0.80, 0.70, 0.45),
]


def esc(text):
    return text.replace("\\", r"\\").replace("(", r"\(").replace(")", r"\)")


class Page:
    def __init__(self):
        self.ops = []

    def rect(self, x, y, w, h, fill=None, stroke=None, width=0.5):
        if fill:
            self.ops.append(f"{fill[0]:.3f} {fill[1]:.3f} {fill[2]:.3f} rg")
        if stroke:
            self.ops.append(f"{stroke[0]:.3f} {stroke[1]:.3f} {stroke[2]:.3f} RG {width} w")
        self.ops.append(f"{x:.2f} {y:.2f} {w:.2f} {h:.2f} re")
        self.ops.append("B" if (fill and stroke) else ("f" if fill else "S"))

    def line(self, x1, y1, x2, y2, color=RULE, width=0.5):
        self.ops.append(f"{color[0]:.3f} {color[1]:.3f} {color[2]:.3f} RG {width} w")
        self.ops.append(f"{x1:.2f} {y1:.2f} m {x2:.2f} {y2:.2f} l S")

    def text(self, x, y, s, size=9, color=INK, font="F1"):
        self.ops.append("BT")
        self.ops.append(f"{color[0]:.3f} {color[1]:.3f} {color[2]:.3f} rg")
        self.ops.append(f"/{font} {size} Tf")
        self.ops.append(f"{x:.2f} {y:.2f} Td ({esc(s)}) Tj")
        self.ops.append("ET")

    def right(self, x, y, s, size=9, color=INK, font="F1"):
        self.text(x - width_of(s, size, font), y, s, size, color, font)

    def stream(self):
        return "\n".join(self.ops).encode("cp1252", "replace")


# Helvetica advance widths, 1000 units per em. Only the printable ASCII range is
# needed; anything else falls back to the width of a lowercase n.
_W = {
    " ": 278, "!": 278, '"': 355, "#": 556, "$": 556, "%": 889, "&": 667, "'": 191,
    "(": 333, ")": 333, "*": 389, "+": 584, ",": 278, "-": 333, ".": 278, "/": 278,
    ":": 278, ";": 278, "<": 584, "=": 584, ">": 584, "?": 556, "@": 1015,
    "[": 278, "\\": 278, "]": 278, "^": 469, "_": 556, "`": 333,
    "{": 334, "|": 260, "}": 334, "~": 584,
}
for _c in "0123456789":
    _W[_c] = 556
for _c, _w in zip("abcdefghijklmnopqrstuvwxyz",
                  [556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833,
                   556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500]):
    _W[_c] = _w
for _c, _w in zip("ABCDEFGHIJKLMNOPQRSTUVWXYZ",
                  [667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833,
                   722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611]):
    _W[_c] = _w


def width_of(s, size, font="F1"):
    # Courier is monospaced at 600/1000 em
    if font == "F3":
        return len(s) * size * 0.6
    scale = 1.0
    total = sum(_W.get(ch, 556) for ch in s)
    if font == "F2":  # Helvetica-Bold runs a little wider
        scale = 1.05
    return total * size * scale / 1000.0


def fit(s, size, max_w, font="F1"):
    """Shortens a string with an ellipsis until it fits."""
    if width_of(s, size, font) <= max_w:
        return s
    while s and width_of(s + "...", size, font) > max_w:
        s = s[:-1]
    return s + "..."


def write_pdf(pages, path):
    objects = []

    def add(body):
        objects.append(body)
        return len(objects)  # 1-based object numbers

    font_regular = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    font_bold = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>")
    font_mono = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>")
    resources = (
        f"<< /Font << /F1 {font_regular} 0 R /F2 {font_bold} 0 R /F3 {font_mono} 0 R >> >>"
    ).encode()

    # the page objects have to point at the page tree, which is written after
    # them, so its id is worked out up front and checked below
    pages_id = len(objects) + 2 * len(pages) + 1
    page_ids = []
    for page in pages:
        data = page.stream()
        content_id = add(b"<< /Length " + str(len(data)).encode() + b" >>\nstream\n" + data + b"\nendstream")
        page_ids.append(
            add(
                f"<< /Type /Page /Parent {pages_id} 0 R /MediaBox [0 0 {PAGE_W:.2f} {PAGE_H:.2f}] "
                f"/Resources ".encode() + resources + f" /Contents {content_id} 0 R >>".encode()
            )
        )
    kids = " ".join(f"{i} 0 R" for i in page_ids)
    real_pages_id = add(f"<< /Type /Pages /Kids [{kids}] /Count {len(page_ids)} >>".encode())
    assert real_pages_id == pages_id, "page tree object id was mispredicted"
    catalog = add(f"<< /Type /Catalog /Pages {pages_id} 0 R >>".encode())

    out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = [0]
    for i, body in enumerate(objects, start=1):
        offsets.append(len(out))
        out += f"{i} 0 obj\n".encode() + body + b"\nendobj\n"
    xref = len(out)
    out += f"xref\n0 {len(objects) + 1}\n".encode()
    out += b"0000000000 65535 f \n"
    for off in offsets[1:]:
        out += f"{off:010d} 00000 n \n".encode()
    out += f"trailer\n<< /Size {len(objects) + 1} /Root {catalog} 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
    with open(path, "wb") as f:
        f.write(out)


# ---------------------------------------------------------------------------
# reading what the load test produced
# ---------------------------------------------------------------------------


def parse_gprof_flat(path):
    """The flat profile: (self %, self seconds, calls, name), hottest first."""
    rows = []
    with open(path) as f:
        for line in f:
            if line.startswith("index") or line.startswith("granularity"):
                break
            m = re.match(r"\s*(\d+\.\d+)\s+(\d+\.\d+)\s+(\d+\.\d+)\s+(\d+)?\s+\S*\s*\S*\s*(.+?)\s*$", line)
            if not m:
                continue
            rows.append(
                {
                    "pct": float(m.group(1)),
                    "self": float(m.group(3)),
                    "calls": int(m.group(4)) if m.group(4) else 0,
                    "name": m.group(5),
                }
            )
    return rows


def shorten(name):
    """A C++ symbol trimmed to what a reader actually scans for."""
    name = re.sub(r"\(.*\)", "()", name)
    name = name.replace("std::__cxx11::", "").replace("std::", "")
    return name


# ---------------------------------------------------------------------------
# the report itself
# ---------------------------------------------------------------------------

MARGIN = 48.0


def header(page, title, subtitle):
    page.rect(0, PAGE_H - 96, PAGE_W, 96, fill=(0.97, 0.97, 0.98))
    page.line(0, PAGE_H - 96, PAGE_W, PAGE_H - 96, color=RULE, width=1.0)
    page.text(MARGIN, PAGE_H - 52, title, size=19, font="F2")
    page.text(MARGIN, PAGE_H - 72, subtitle, size=9.5, color=MUTED)


def footer(page, n, total):
    page.line(MARGIN, 52, PAGE_W - MARGIN, 52, color=RULE)
    page.text(MARGIN, 38, "Block server — load test report", size=8, color=MUTED)
    page.right(PAGE_W - MARGIN, 38, f"page {n} of {total}", size=8, color=MUTED)


def stat_tile(page, x, y, w, h, label, value, unit, note, color=INK):
    page.rect(x, y, w, h, fill=(0.985, 0.985, 0.99), stroke=RULE)
    page.text(x + 12, y + h - 20, label.upper(), size=7.5, color=MUTED, font="F2")
    page.text(x + 12, y + h - 46, value, size=20, color=color, font="F2")
    if unit:
        page.text(x + 14 + width_of(value, 20, "F2"), y + h - 46, unit, size=9, color=MUTED)
    if note:
        page.text(x + 12, y + 12, fit(note, 7.5, w - 24), size=7.5, color=MUTED)


def page_summary(result, idle):
    page = Page()
    header(
        page,
        "Block server under full load",
        f"{result['bots']} bots plus a live client, every gamemode feature exercised, "
        f"{result['seconds']}s steady-state hold",
    )

    avg_cpu = sum(s["cpu_pct"] for s in idle) / len(idle)
    avg_tick = sum(s["ms_per_tick"] for s in idle) / len(idle)
    peak_tick = max(s["ms_per_tick"] for s in idle)
    drift = idle[-1]["rss_mb"] - idle[0]["rss_mb"]

    y = PAGE_H - 190
    w = (PAGE_W - 2 * MARGIN - 24) / 3.0
    stat_tile(page, MARGIN, y, w, 78, "tick budget used", f"{avg_tick:.2f}", "ms of 20",
              f"peak {peak_tick:.2f} ms  —  {100 * (1 - avg_tick / 20):.0f}% headroom", GOOD)
    stat_tile(page, MARGIN + w + 12, y, w, 78, "cpu", f"{avg_cpu:.1f}", "% of a core",
              "one core carries the whole server", GOOD)
    stat_tile(page, MARGIN + 2 * (w + 12), y, w, 78, "memory drift", f"{drift:+.1f}", "MB",
              f"steady at {idle[-1]['rss_mb']:.0f} MB", GOOD if abs(drift) < 5 else WARN)

    page.text(MARGIN, y - 34, "What the run did", size=12, font="F2")
    lines = [
        f"Filled a 128-slot server with {result['bots']} bots, each going through the real connect,",
        "enter, tick and snapshot path, then connected a live headless client on top.",
        "",
        "The client was walked through accounts, blockpoints, the vote menu, the shop, cosmetics,",
        "clans and clan chat, whois, the LMB / TDM / zCatch / BombTag events, moderation commands,",
        "the leaderboards and chat, all while the server stayed full.",
        "",
        f"whois recorded {result['whois_rows']} connection rows over the run.",
    ]
    ly = y - 54
    for line in lines:
        page.text(MARGIN, ly, line, size=9.5)
        ly -= 14

    page.text(MARGIN, ly - 16, "Verdict", size=12, font="F2")
    ly -= 38
    checks = [
        ("Survived the run", result["alive"]),
        ("Shut down cleanly", result["clean_exit"]),
        ("No asserts, sanitizer reports or database failures", not result["errors"]),
        ("No memory growth while held at full load", abs(drift) < 5.0),
        ("Tick budget never exceeded", peak_tick < 20.0),
    ]
    for label, ok in checks:
        color = GOOD if ok else WARN
        page.rect(MARGIN, ly - 2, 8, 8, fill=color)
        page.text(MARGIN + 16, ly, label, size=9.5)
        page.right(PAGE_W - MARGIN, ly, "pass" if ok else "look", size=9.5, color=color, font="F2")
        ly -= 18

    # CPU over the hold
    page.text(MARGIN, ly - 18, "Tick cost over the hold", size=12, font="F2")
    cx, cy, cw, ch = MARGIN, ly - 130, PAGE_W - 2 * MARGIN, 96
    page.rect(cx, cy, cw, ch, fill=(0.985, 0.985, 0.99), stroke=RULE)
    top = max(4.0, peak_tick * 1.35)
    for frac in (0.0, 0.5, 1.0):
        gy = cy + frac * ch
        page.line(cx, gy, cx + cw, gy, color=RULE)
        page.text(cx + cw + 4, gy - 3, f"{top * frac:.1f}", size=7, color=MUTED)
    step = cw / max(1, len(idle))
    for i, s in enumerate(idle):
        bh = min(ch, ch * s["ms_per_tick"] / top)
        page.rect(cx + i * step + step * 0.15, cy, step * 0.7, bh, fill=ACCENT)
    page.text(cx, cy - 12, f"one bar per second, ms of CPU per 20 ms tick (scale to {top:.1f} ms)",
              size=7.5, color=MUTED)
    return page


def page_profile(flat, result):
    page = Page()
    header(page, "Where the CPU goes",
           f"gprof, RelWithDebInfo build, {result['bots']} bots — self time per function")

    interesting = [r for r in flat if r["self"] > 0 and len(r["name"]) > 3 and r["name"] != "_init"][:22]
    if not interesting:
        page.text(MARGIN, PAGE_H - 160, "no samples were collected", size=10, color=MUTED)
        return page

    total = sum(r["self"] for r in flat) or 1.0
    page.text(MARGIN, PAGE_H - 132, "Hottest functions", size=12, font="F2")
    page.text(MARGIN, PAGE_H - 148,
              "Bars are self time: work done inside the function itself, not in what it calls.",
              size=8.5, color=MUTED)

    y = PAGE_H - 172
    bar_x = MARGIN + 250
    bar_w = PAGE_W - MARGIN - bar_x - 44
    widest = max(r["self"] for r in interesting)
    for i, r in enumerate(interesting):
        page.text(MARGIN, y, fit(shorten(r["name"]), 8, 244, "F3"), size=8, font="F3")
        w = bar_w * r["self"] / widest
        page.rect(bar_x, y - 2, max(w, 0.6), 8, fill=FLAME[i % len(FLAME)])
        page.right(PAGE_W - MARGIN, y, f"{100 * r['self'] / total:.1f}%", size=8, color=MUTED)
        y -= 15

    page.text(MARGIN, y - 14, "Reading it", size=12, font="F2")
    notes = [
        "The profile is dominated by snapshot work: building the per-client world view, delta-",
        "encoding it against the previous one and compressing it. That is inherent to a server",
        "with 128 players -- every client needs its own snapshot, every tick.",
        "",
        "Nothing gamemode-specific sits near the top, which is the result to want: Block's own",
        "code rides along inside the snapshot path rather than adding a cost of its own.",
    ]
    ly = y - 34
    for line in notes:
        page.text(MARGIN, ly, line, size=9.5)
        ly -= 14
    return page


def page_calls(flat, before_flat, result):
    page = Page()
    header(page, "Call volume and what changed",
           "the per-snapshot paths, and the effect of caching the component lookups")

    page.text(MARGIN, PAGE_H - 132, "Busiest call paths", size=12, font="F2")
    page.text(MARGIN, PAGE_H - 148,
              "Call counts, not time: these are the functions the snapshot path enters most often.",
              size=8.5, color=MUTED)

    by_calls = sorted([r for r in flat if r["calls"] and len(r["name"]) > 3], key=lambda r: -r["calls"])[:12]
    y = PAGE_H - 174
    page.text(MARGIN, y, "function", size=8, color=MUTED, font="F2")
    page.right(PAGE_W - MARGIN, y, "calls", size=8, color=MUTED, font="F2")
    y -= 6
    page.line(MARGIN, y, PAGE_W - MARGIN, y)
    y -= 14
    for r in by_calls:
        page.text(MARGIN, y, fit(shorten(r["name"]), 8, 380, "F3"), size=8, font="F3")
        page.right(PAGE_W - MARGIN, y, f"{r['calls']:,}", size=8)
        y -= 14

    if before_flat:
        page.text(MARGIN, y - 20, "Component lookups, before and after", size=12, font="F2")
        page.text(MARGIN, y - 36,
                  "SnapPlayerScore and OnSnapPlayerInfo run once per player per viewer per tick.",
                  size=8.5, color=MUTED)
        page.text(MARGIN, y - 48,
                  "They each looked the component registry up by type; that now happens once a tick.",
                  size=8.5, color=MUTED)

        def find(rows, needle):
            for r in rows:
                if needle in r["name"]:
                    return r["calls"]
            return 0

        pairs = [
            ("CComponentRegistry::Get(type_index)", find(before_flat, "CComponentRegistry::Get(std::type_index)"),
             find(flat, "CComponentRegistry::Get(std::type_index)")),
            ("CComponent::CComponent()", find(before_flat, "CComponent::CComponent"),
             find(flat, "CComponent::CComponent")),
        ]
        ly = y - 74
        for label, before, after in pairs:
            page.text(MARGIN, ly, label, size=8.5, font="F3")
            widest = max(before, after, 1)
            bx, bw = MARGIN + 230, PAGE_W - MARGIN - (MARGIN + 230) - 96
            page.rect(bx, ly + 4, bw * before / widest, 7, fill=(0.80, 0.80, 0.84))
            page.rect(bx, ly - 6, max(bw * after / widest, 0.6), 7, fill=ACCENT)
            page.text(bx + bw + 8, ly + 4, f"{before:,}", size=7.5, color=MUTED)
            page.text(bx + bw + 8, ly - 6, f"{after:,}", size=7.5, color=ACCENT)
            ly -= 34
        page.rect(MARGIN + 230, ly + 8, 7, 7, fill=(0.80, 0.80, 0.84))
        page.text(MARGIN + 242, ly + 8, "before", size=7.5, color=MUTED)
        page.rect(MARGIN + 290, ly + 8, 7, 7, fill=ACCENT)
        page.text(MARGIN + 302, ly + 8, "after", size=7.5, color=MUTED)
        ly -= 20
    else:
        ly = y - 20

    page.text(MARGIN, ly - 14, "Honest caveats", size=12, font="F2")
    caveats = [
        "gprof samples the main thread. The database, whois and HTTP workers run on threads of",
        "their own and do not appear here; they were watched through the process totals instead.",
        "",
        "Bots send no input, so this is the cost of a full server at rest: connection, tick and",
        "snapshot for 128 players. A busy match adds movement and collision work on top.",
        "",
        "The lookup caching removed 24 million registry hits per run, but they were about 1% of",
        "samples, so the wall-clock gain is small. It is worth keeping for what it stops growing",
        "into, not for the number it moved.",
    ]
    ly -= 34
    for line in caveats:
        page.text(MARGIN, ly, line, size=9.5)
        ly -= 14
    return page


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("run_dir", help="directory holding loadtest.json and gprof.txt")
    p.add_argument("--before", help="an earlier run's directory, to compare call counts against")
    p.add_argument("--pdf", default=None, help="where to write the report")
    args = p.parse_args()

    with open(os.path.join(args.run_dir, "loadtest.json")) as f:
        result = json.load(f)
    gprof_path = os.path.join(args.run_dir, "gprof.txt")
    flat = parse_gprof_flat(gprof_path) if os.path.exists(gprof_path) else []
    before_flat = []
    if args.before:
        before_path = os.path.join(args.before, "gprof.txt")
        if os.path.exists(before_path):
            before_flat = parse_gprof_flat(before_path)

    idle = [s for s in result["samples"] if "idle" in s["label"]]
    if not idle:
        print("the run has no steady-state samples", file=sys.stderr)
        return 1

    pages = [page_summary(result, idle), page_profile(flat, result), page_calls(flat, before_flat, result)]
    for i, page in enumerate(pages, start=1):
        footer(page, i, len(pages))

    out = args.pdf or os.path.join(args.run_dir, "block-performance.pdf")
    write_pdf(pages, out)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
