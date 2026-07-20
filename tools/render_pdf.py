#!/usr/bin/env python3
"""
arabic-led-text-pipeline - markdown to pageless PDF
Copyright (C) 2026  Mulham Fetna
SPDX-License-Identifier: AGPL-3.0-or-later

Renders a Markdown file to a single continuous PDF page - no page breaks, no
headers, no orphaned headings. Useful when a document is meant to be read by
scrolling rather than printed.

Pandoc converts Markdown to HTML; headless Chrome prints it. The page height is
measured from the rendered document and fed back as the paper size, which is
what makes the output one unbroken page.

    python3 tools/render_pdf.py docs/seminar.md
    python3 tools/render_pdf.py docs/seminar.ar.md --rtl

Requires: pandoc, google-chrome (or chromium).
"""

import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

CSS = """
@page { margin: 0; size: %(w)smm %(h)smm; }
* { box-sizing: border-box; }
body {
  margin: 0; padding: 22mm 20mm;
  font-family: %(font)s;
  font-size: 10.5pt; line-height: 1.62; color: #14171a;
  direction: %(dir)s;
  -webkit-print-color-adjust: exact; print-color-adjust: exact;
}
h1, h2, h3 { line-height: 1.28; color: #0d1117; margin: 1.6em 0 .55em; }
h1 { font-size: 21pt; margin-top: 0; letter-spacing: -.01em; }
h2 { font-size: 15pt; padding-bottom: .3em; border-bottom: 1.5px solid #d8dee4; }
h3 { font-size: 12pt; }
p, li { orphans: 3; widows: 3; }
code {
  font-family: "DejaVu Sans Mono", monospace; font-size: 9pt;
  background: #f2f4f7; padding: .12em .34em; border-radius: 3px;
  direction: ltr; unicode-bidi: embed;
}
pre {
  background: #f6f8fa; border: 1px solid #dfe3e8; border-radius: 6px;
  padding: 11px 13px; overflow-x: auto; direction: ltr; text-align: left;
}
pre code { background: none; padding: 0; font-size: 8.6pt; line-height: 1.48; }
table { border-collapse: collapse; width: 100%%; margin: 1em 0; font-size: 9.6pt; }
th, td { border: 1px solid #d8dee4; padding: 6px 9px; text-align: %(align)s; vertical-align: top; }
th { background: #f2f4f7; font-weight: 600; }
blockquote {
  margin: 1.1em 0; padding: .55em 1.1em;
  border-%(side)s: 3.5px solid #2f81f7; background: #f6f9fe; color: #2f363d;
}
a { color: #0a58ca; text-decoration: none; }
hr { border: 0; border-top: 1px solid #dfe3e8; margin: 2em 0; }
img { max-width: 100%%; }
"""


def page_count(pdf: pathlib.Path) -> int:
    """Page count via pdfinfo when present, else by counting /Type /Page."""
    if shutil.which("pdfinfo"):
        out = subprocess.run(["pdfinfo", str(pdf)], capture_output=True, text=True)
        for line in out.stdout.splitlines():
            if line.startswith("Pages:"):
                return int(line.split()[1])
    import re
    return max(1, len(re.findall(rb"/Type\s*/Page[^s]", pdf.read_bytes())))


def chrome_binary() -> str:
    for name in ("google-chrome", "chromium", "chromium-browser", "google-chrome-stable"):
        path = shutil.which(name)
        if path:
            return path
    sys.exit("no Chrome/Chromium found - install one, or use --html to stop at HTML")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source", type=pathlib.Path)
    ap.add_argument("-o", "--out", type=pathlib.Path)
    ap.add_argument("--rtl", action="store_true", help="right-to-left layout (Arabic)")
    ap.add_argument("--width", type=int, default=210, help="page width in mm")
    ap.add_argument("--html", action="store_true", help="emit HTML and stop")
    args = ap.parse_args()

    if not args.source.exists():
        sys.exit(f"no such file: {args.source}")
    out = args.out or args.source.with_suffix(".pdf")

    # Amiri and IBM Plex Sans Arabic render Arabic properly; DejaVu does not
    # shape it well at body sizes.
    font = ('"IBM Plex Sans Arabic", "Amiri", "DejaVu Sans", sans-serif'
            if args.rtl else '"DejaVu Sans", "Helvetica Neue", Arial, sans-serif')

    body = subprocess.run(
        ["pandoc", str(args.source), "-f", "gfm", "-t", "html5", "--no-highlight"],
        capture_output=True, text=True, check=True).stdout

    css_vars = {
        "font": font, "dir": "rtl" if args.rtl else "ltr",
        "align": "right" if args.rtl else "left",
        "side": "right" if args.rtl else "left",
        "w": args.width, "h": 300,          # provisional; remeasured below
    }

    def build(height_mm):
        css_vars["h"] = height_mm
        return (f'<!doctype html><html dir="{css_vars["dir"]}" lang='
                f'"{"ar" if args.rtl else "en"}"><meta charset="utf-8">'
                f"<style>{CSS % css_vars}</style><body>{body}</body></html>")

    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        html = td / "doc.html"
        html.write_text(build(300), encoding="utf-8")

        if args.html:
            target = out.with_suffix(".html")
            target.write_text(html.read_text(encoding="utf-8"), encoding="utf-8")
            print(f"wrote {target}")
            return

        chrome = chrome_binary()
        common = ["--headless", "--disable-gpu", "--no-sandbox",
                  "--no-pdf-header-footer", f"--user-data-dir={td/'profile'}"]

        def render(height_mm):
            html.write_text(build(height_mm), encoding="utf-8")
            subprocess.run(
                [chrome, *common, "--virtual-time-budget=8000",
                 f"--print-to-pdf={out}", html.as_uri()],
                capture_output=True, timeout=240, check=True)
            return page_count(out)

        """
        Converge on a single page by measurement rather than estimation.

        Guessing the height from character counts is unreliable - tables and
        code blocks expand unpredictably during layout, and being short by any
        amount reintroduces exactly the page break this tool exists to avoid.
        Rendering and counting is slower but actually correct: if the document
        needed N pages at height H, it fits in roughly H*N, and one or two
        iterations settle it.
        """
        height_mm = 420
        for attempt in range(6):
            pages = render(height_mm)
            if pages <= 1:
                break
            # Slight overshoot; a page break costs another whole round trip.
            height_mm = int(height_mm * pages * 1.06) + 40
        else:
            print(f"warning: still {pages} pages at {height_mm}mm", file=sys.stderr)

    if not out.exists():
        sys.exit("chrome produced no output")

    print(f"wrote {out}  ({out.stat().st_size // 1024} KB, "
          f"single page {args.width}x{height_mm}mm)")


if __name__ == "__main__":
    main()
