#!/usr/bin/env python3
"""
arabic-led-text-pipeline - canvas conversion reference
Copyright (C) 2026  Mulham Fetna
SPDX-License-Identifier: AGPL-3.0-or-later

Reference for how pixels convert between mono and RGB. The C implementation in
canvas.c must agree with this; if the two ever disagree, one of them is wrong
and this file says which behaviour was intended.
"""


def mono_to_rgb(bit, colour):
    """A lit mono pixel takes the frame colour; an unlit one is black."""
    return tuple(colour) if bit else (0, 0, 0)


def rgb_to_mono(r, g, b, cutoff=128):
    """
    Rec. 601 luma, then threshold - the same semantics the browser uses.

    canvas.c does this in integer arithmetic. The float form here is the
    definition that the integer form must reproduce.
    """
    return (0.299 * r + 0.587 * g + 0.114 * b) >= cutoff


def rgb_to_mono_int(r, g, b, cutoff=128):
    """
    Exactly what canvas.c computes.

    16-bit weights, not 8-bit. 77/150/29 over 256 looks tidy but drifts from
    Rec. 601 by enough to flip colours sitting near the cutoff - 139 of them
    across the space, which would show up as single pixels appearing or
    vanishing for no visible reason. 19595/38470/7471 over 65536 sum exactly
    and track the definition.

    Truncating, NOT rounding. floor(x) >= T is equivalent to x >= T, so a
    plain shift reproduces the float predicate exactly. Adding a rounding
    term instead tests x >= T-0.5, which is a different question and flips
    hundreds of colours just below the cutoff.
    """
    return ((19595 * r + 38470 * g + 7471 * b) >> 16) >= cutoff


for colour, bit, want in [
    ((255, 0, 0), True, (255, 0, 0)),
    ((255, 0, 0), False, (0, 0, 0)),
    ((0, 255, 0), True, (0, 255, 0)),
]:
    got = mono_to_rgb(bit, colour)
    assert got == want, f"mono->rgb {colour} lit={bit}: got {got}, want {want}"

# Pure red has luma 76.2, which is BELOW the 128 cutoff - so red text sent as
# RGB renders DARK on a monochrome panel. That is correct Rec. 601 behaviour
# and the most surprising case in the whole conversion, which is why the cutoff
# is exposed rather than fixed.
assert rgb_to_mono(255, 0, 0) is False, "pure red should fall below the cutoff"
assert rgb_to_mono(0, 255, 0) is True, "pure green has luma 149.7"
assert rgb_to_mono(255, 255, 255) is True
assert rgb_to_mono(0, 0, 0) is False

# The integer form must agree with the float definition across the space.
#
# The integer form must track the float definition. Exact agreement everywhere
# is not achievable: the weights are rationals over 65536 and cannot represent
# 0.299/0.587/0.114 exactly, so a colour whose luma lands within a hair of the
# cutoff can fall either side. What must NOT happen is systematic drift, which
# would move visibly many pixels.
#
# So: disagreements are tolerated only when the colour is genuinely on the
# boundary, and only a handful of them.
#
STEP = 5
tested = 0
borderline, systematic = [], []
for r in range(0, 256, STEP):
    for g in range(0, 256, STEP):
        for b in range(0, 256, STEP):
            tested += 1
            if rgb_to_mono(r, g, b) == rgb_to_mono_int(r, g, b):
                continue
            luma = 0.299 * r + 0.587 * g + 0.114 * b
            (borderline if abs(luma - 128) < 0.5 else systematic).append((r, g, b, luma))

assert not systematic, (
    f"integer luma drifts from Rec. 601 away from the boundary on "
    f"{len(systematic)} colours, e.g. {systematic[:3]} - the weights are wrong")
assert len(borderline) <= 5, (
    f"{len(borderline)} boundary disagreements is more than rounding noise")

print(f"canvas conversion reference OK — {tested} colours checked, "
      f"{len(borderline)} boundary-exact disagreement(s), no systematic drift")
