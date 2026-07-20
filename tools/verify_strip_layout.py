#!/usr/bin/env python3
"""
arabic-led-text-pipeline - WS2812B layout reference
Copyright (C) 2026  Mulham Fetna
SPDX-License-Identifier: AGPL-3.0-or-later

A strip is one long 1-D chain folded into a matrix. This defines which LED in
the chain sits at (x, y) under each folding, and ws2812b.c must agree.

Getting this wrong does not fail loudly - it scrambles every other row, which
looks like a corrupt font rather than a wiring assumption.
"""


def strip_index(x, y, width, serpentine):
    """
    Progressive: every row runs the same direction, needing a return wire from
    the end of each row back to the start of the next.

    Serpentine: alternate rows run backwards, so the strip simply folds at the
    end of each row. This is how most hand-built matrices are wired because it
    needs no return wires at all.
    """
    if serpentine and (y % 2):
        x = width - 1 - x
    return y * width + x


W, H = 8, 8

# Progressive: row 1 starts immediately after row 0, same direction.
assert strip_index(0, 0, W, False) == 0
assert strip_index(7, 0, W, False) == 7
assert strip_index(0, 1, W, False) == 8
assert strip_index(7, 1, W, False) == 15

# Serpentine: row 1 runs backwards, so LED 8 is at the FAR end of that row.
assert strip_index(0, 0, W, True) == 0
assert strip_index(7, 0, W, True) == 7
assert strip_index(7, 1, W, True) == 8, "serpentine row 1 must start at x=7"
assert strip_index(0, 1, W, True) == 15
assert strip_index(0, 2, W, True) == 16, "even rows run forwards again"

# Both foldings must be bijective over the panel: every pixel maps to exactly
# one LED, and every LED is used. A collision here would silently drop pixels.
for serp in (False, True):
    seen = [strip_index(x, y, W, serp) for y in range(H) for x in range(W)]
    assert len(set(seen)) == W * H, (
        f"serpentine={serp}: {len(set(seen))} unique indices for {W*H} pixels")
    assert min(seen) == 0 and max(seen) == W * H - 1, (
        f"serpentine={serp}: indices span {min(seen)}..{max(seen)}")

# A single straight strip is the degenerate case, and both foldings agree there
# because there is no second row to reverse.
for x in range(16):
    assert strip_index(x, 0, 16, False) == strip_index(x, 0, 16, True) == x

print(f"strip layout OK — both foldings bijective over {W}x{H}, "
      f"straight-line case consistent")
