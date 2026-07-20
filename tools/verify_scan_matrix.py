#!/usr/bin/env python3
"""
arabic-led-text-pipeline - scanned matrix reference
Copyright (C) 2026  Mulham Fetna
SPDX-License-Identifier: AGPL-3.0-or-later

Defines what scan_matrix.c must produce: the byte order on the chain, the
one-hot row walk, and the row/phase relationship for both selection
strategies.

Chain inversion is the classic error here - it does not fail loudly, it just
puts the image in the wrong chip - so it is pinned down before any C is
written.
"""

# ─── shift-register row select (DIY 74HC595) ──────────────────────────────

def shift_reg_frame(row, row_regs, col_regs, col_data):
    """
    Bytes to clock out for one scan row, in transmission order.

    The chain is ESP32 -> [row regs] -> [column regs]. Data shifts THROUGH, so
    the byte sent first ends up in the chip furthest along - the last column
    register. Column bytes are therefore transmitted before row bytes.

    Getting this backwards puts the row pattern into the column registers,
    which lights a meaningless smear rather than failing cleanly.
    """
    assert 0 <= row < row_regs * 8, f"row {row} outside {row_regs*8} rows"
    assert len(col_data) == col_regs, f"expected {col_regs} column bytes"

    # One-hot: exactly one bit set across the whole row-select chain.
    rows = [0] * row_regs
    rows[row // 8] = 0x80 >> (row % 8)

    # Columns first (they travel furthest), then rows.
    return list(col_data) + rows


def binary_row_select(phase):
    """P10: two address lines carry the phase, no bytes on the chain."""
    return (phase & 1, (phase >> 1) & 1)


# ─── checks ───────────────────────────────────────────────────────────────

ROW_REGS, COL_REGS = 2, 4          # 32 x 16 panel
ROWS = ROW_REGS * 8
cols = [0xAA, 0xBB, 0xCC, 0xDD]

# Transmission order: columns precede rows.
f = shift_reg_frame(0, ROW_REGS, COL_REGS, cols)
assert f[:COL_REGS] == cols, "column bytes must be transmitted first"
assert len(f) == COL_REGS + ROW_REGS

# Exactly one row bit set, anywhere in the chain, for every row.
for r in range(ROWS):
    rows_part = shift_reg_frame(r, ROW_REGS, COL_REGS, cols)[COL_REGS:]
    bits = sum(bin(b).count("1") for b in rows_part)
    assert bits == 1, f"row {r}: {bits} bits set, expected exactly 1"

# The walk must cover every row exactly once - a duplicate would light one row
# twice as bright and leave another dark.
seen = [tuple(shift_reg_frame(r, ROW_REGS, COL_REGS, cols)[COL_REGS:])
        for r in range(ROWS)]
assert len(set(seen)) == ROWS, f"row walk covers {len(set(seen))} of {ROWS}"

# Row 0 is the most significant bit of the first register; row 8 moves to the
# second register. Off-by-one here shifts the whole image vertically.
assert shift_reg_frame(0, ROW_REGS, COL_REGS, cols)[COL_REGS:] == [0x80, 0x00]
assert shift_reg_frame(7, ROW_REGS, COL_REGS, cols)[COL_REGS:] == [0x01, 0x00]
assert shift_reg_frame(8, ROW_REGS, COL_REGS, cols)[COL_REGS:] == [0x00, 0x80]

# P10: four phases from two address lines, each distinct.
phases = [binary_row_select(p) for p in range(4)]
assert len(set(phases)) == 4, "A/B must give four distinct phases"
assert phases[0] == (0, 0) and phases[3] == (1, 1)

# 1/4 scan: phase p lights rows p, p+4, p+8, p+12 - together covering all 16
# exactly once, which is what makes the scan complete.
covered = sorted(p + r * 4 for p in range(4) for r in range(4))
assert covered == list(range(16)), "1/4 scan must cover all 16 rows once"

print(f"scan matrix reference OK — {ROWS}-row walk bijective, "
      f"chain order columns-then-rows, 1/4 scan covers 16 rows")
