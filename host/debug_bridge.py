#!/usr/bin/env python3
"""
arabic-led-text-pipeline - desktop debug bridge
Copyright (C) 2026  Mulham Fetna
SPDX-License-Identifier: AGPL-3.0-or-later

Serves the firmware's own web UI on localhost and forwards the frames it posts
to the panel over the USB-serial cable, so the whole pipeline can be debugged
in a desktop browser with devtools open - no phone, no WiFi, no AP join.

The page served is the exact file embedded in the firmware, so what is debugged
here is what ships. Only the transport differs: HTTP POST /frame becomes a
framed UART write.

    python3 host/debug_bridge.py --port /dev/ttyUSB0
    # then open http://127.0.0.1:8080/

Add --watch to also stream the device's log output to this terminal.
"""

import argparse
import http.server
import json
import pathlib
import socketserver
import struct
import sys
import threading
import zlib

try:
    import serial
except ImportError:
    sys.exit("pyserial missing - install with: pip install pyserial")

REPO = pathlib.Path(__file__).resolve().parent.parent
INDEX = REPO / "firmware" / "main" / "www" / "index.html"

SOH     = 0x01
VERSION = 2

_ser = None
_ser_lock = threading.Lock()


FLAG_SCROLL    = 0x01
FLAG_RIGHTWARD = 0x02

FMT_MONO = 0
FMT_RGB  = 1


def build_frame(payload: bytes, w: int, h_rows: int,
                scroll: bool = False, speed_ms: int = 60,
                rightward: bool = False, fmt: int = FMT_MONO,
                colour: tuple = (255, 255, 255)) -> bytes:
    """
    Wire format v2:

        SOH | ver | w | h | fmt | flags | speed | R | G | B | payload | crc32

    `w` is width in BYTES for mono and in PIXELS for RGB, matching what the
    firmware expects for each format.

    CRC covers the nine header bytes plus the payload, matching frame_crc32()
    in the firmware. zlib.crc32 is CRC-32/ISO-HDLC, the same polynomial and
    conventions the MCU implements - which is exactly why that variant was
    chosen for the protocol.
    """
    flags = (FLAG_SCROLL if scroll else 0) | (FLAG_RIGHTWARD if rightward else 0)
    header = struct.pack("BBBBBB", VERSION, w, h_rows, fmt, flags,
                         max(10, min(255, speed_ms))) + bytes(colour)
    crc = zlib.crc32(header + payload) & 0xFFFFFFFF
    return bytes([SOH]) + header + payload + struct.pack("<I", crc)


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # the interesting logging is done explicitly below

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            body = INDEX.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            # Always re-read from disk: editing index.html and hitting reload
            # must show the change, or the debug loop is a lie.
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/panel":
            # Mirrors the firmware's /panel, including whether the attached
            # panel can show colour - without it the page never offers the RGB
            # path and the bridge cannot debug the thing it exists to debug.
            body = json.dumps({"width": ARGS.width, "height": ARGS.height,
                               "colour": ARGS.colour}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_error(404)

    def do_POST(self):
        if not self.path.startswith("/frame"):
            self.send_error(404)
            return

        qs = dict(
            kv.split("=", 1)
            for kv in self.path.partition("?")[2].split("&")
            if "=" in kv
        )
        w_bytes = int(qs.get("w", 0))
        h_rows = int(qs.get("h", 0))
        fmt = FMT_RGB if qs.get("fmt") == "1" else FMT_MONO
        payload = self.rfile.read(int(self.headers.get("Content-Length", 0)))

        expected = w_bytes * h_rows * (3 if fmt == FMT_RGB else 1)
        if expected == 0 or len(payload) != expected:
            msg = f"geometry {w_bytes}x{h_rows} implies {expected} bytes, got {len(payload)}"
            print(f"  ✗ {msg}")
            self.send_error(400, msg)
            return

        scroll = qs.get("mode") == "scroll"
        speed = int(qs.get("speed", 60))
        rightward = qs.get("dir") == "rtl"
        fmt = FMT_RGB if qs.get("fmt") == "1" else FMT_MONO
        rgb = qs.get("rgb", "ffffff")
        colour = (int(rgb[0:2], 16), int(rgb[2:4], 16), int(rgb[4:6], 16))
        frame = build_frame(payload, w_bytes, h_rows, scroll, speed,
                            rightward, fmt, colour)
        with _ser_lock:
            _ser.write(frame)
            _ser.flush()

        px_w = w_bytes if fmt == FMT_RGB else w_bytes * 8
        print(f"  → {px_w}x{h_rows}px, {len(payload)}B payload, "
              f"{'rgb' if fmt == FMT_RGB else 'mono'}, "
              f"{'scroll '+('→RTL' if rightward else '←LTR')+'@'+str(speed)+'ms' if scroll else 'static'}, "
              f"crc={frame[-4:][::-1].hex()}")
        render_ascii(payload, w_bytes, h_rows, fmt)

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"ok":true}')


def render_ascii(payload: bytes, w: int, h_rows: int, fmt: int = FMT_MONO) -> None:
    """
    Print what the panel should show.

    Decodes the payload the same way the firmware does, so a mismatch between
    this and the physical panel isolates the fault to the driver's orientation
    handling rather than to the browser's rendering.

    RGB and mono are decoded differently, and confusing them produces art that
    is meaningless while still looking plausible - worse than printing nothing.
    `w` is pixels for RGB and bytes for mono, matching the wire.
    """
    if fmt == FMT_RGB:
        # Shaded by luma, so the art shows both what a colour panel would light
        # and what a monochrome one would make of the same frame.
        ramp = " .:-=+*#%@"
        for y in range(h_rows):
            row = ""
            for x in range(w):
                i = (y * w + x) * 3
                lum = (0.299 * payload[i] + 0.587 * payload[i + 1]
                       + 0.114 * payload[i + 2])
                row += ramp[min(len(ramp) - 1, int(lum / 256 * len(ramp)))]
            print(f"    |{row}|")
        return

    for y in range(h_rows):
        row = "".join(
            "#" if payload[y * w + (x >> 3)] & (0x80 >> (x & 7)) else "."
            for x in range(w * 8)
        )
        print(f"    |{row}|")


def watch_serial(ser):
    while True:
        try:
            line = ser.readline()
        except Exception:
            return
        if line:
            print("  [dev]", line.decode("utf-8", "replace").rstrip())


def main():
    global _ser, ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--http-port", type=int, default=8080)
    ap.add_argument("--width", type=int, default=8, help="panel width in pixels")
    ap.add_argument("--height", type=int, default=8, help="panel height in pixels")
    ap.add_argument("--colour", action="store_true",
                    help="pretend the panel shows colour, to exercise the RGB path")
    ap.add_argument("--watch", action="store_true", help="stream device logs")
    ARGS = ap.parse_args()

    _ser = serial.Serial(ARGS.port, ARGS.baud, timeout=1)
    # Do NOT toggle DTR/RTS: on a CH340 board those lines are wired to EN and
    # GPIO0, so touching them resets the ESP32 or drops it into the bootloader.
    print(f"serial: {ARGS.port} @ {ARGS.baud}")

    if ARGS.watch:
        threading.Thread(target=watch_serial, args=(_ser,), daemon=True).start()

    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("127.0.0.1", ARGS.http_port), Handler) as httpd:
        print(f"open http://127.0.0.1:{ARGS.http_port}/   "
              f"(panel {ARGS.width}x{ARGS.height}, "
              f"colour={'yes' if ARGS.colour else 'no'})")
        print("serving firmware/main/www/index.html - edit and reload to iterate\n")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nbye")


if __name__ == "__main__":
    main()
