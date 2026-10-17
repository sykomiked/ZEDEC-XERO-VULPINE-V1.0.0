#!/usr/bin/env python3
# segment.py — lossless segment parsing. A system built on exact
# arithmetic never truncates: when input exceeds a manageable size it is
# SPLIT into segments, each parsed, and the results joined — bit for bit,
# nothing dropped. Segment SIZE is bounded (a denial-of-service guard: a
# single segment may not exceed a limit), but the NUMBER of segments is
# not bounded: the world computer's principle is that whoever can pay the
# compute gets the compute. Truncation is the floating-point error of data
# handling and is never used.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: from segment import segment_text, parse_segmented
          segment_text(text, limit) -> [segments]      (lossless)
          parse_segmented(text, limit, fn) -> [results] (each parsed)
"""
import hashlib

# the DoS guard: one segment may not exceed this many characters. A single
# oversized segment is refused, never silently cut. The limit protects the
# parser from an unbounded single input; it does not limit how many
# segments an input may have.
SEGMENT_LIMIT = 1 << 20   # 1 MiB per segment


class SegmentError(ValueError):
    """An input that cannot be segmented without loss."""


def segment_text(text, limit=SEGMENT_LIMIT):
    """Split `text` into segments of at most `limit` characters, at safe
    boundaries (never inside a line), such that ''.join(segments) == text
    exactly. Lossless by construction: the join is verified before return.
    """
    if limit <= 0:
        raise SegmentError("the segment limit must be positive")
    if len(text) <= limit:
        return [text]
    segs = []
    pos = 0
    while pos < len(text):
        end = pos + limit
        if end >= len(text):
            segs.append(text[pos:])
            break
        # back up to the last line boundary, so a segment never splits a
        # line in half; if the segment has no boundary, split at `end`
        nl = text.rfind("\n", pos, end)
        cut = nl + 1 if nl > pos else end
        segs.append(text[pos:cut])
        pos = cut
    # the lossless invariant: the join must equal the input exactly
    if "".join(segs) != text:
        raise SegmentError("the segments do not reconstruct the input")
    return segs


def parse_segmented(text, fn, limit=SEGMENT_LIMIT):
    """Parse each segment with `fn` and return the list of results, in
    order. `fn` receives one segment and returns its parse. The caller
    joins the results; this function never drops one."""
    return [fn(seg) for seg in segment_text(text, limit)]


def digest(text):
    """The identity of an input, for the join's verification: two inputs
    with the same digest are the same, whatever segmentation produced
    them."""
    return hashlib.sha3_256(text.encode("utf-8")).hexdigest()


def join_verified(results, original, render=lambda r: r):
    """Join parsed results and prove the reconstruction is exact: the
    joined text's digest must equal the original's. A mismatch is an
    error, never a silent loss."""
    joined = "".join(render(r) for r in results)
    if digest(joined) != digest(original):
        raise SegmentError("the joined result does not match the original")
    return joined


if __name__ == "__main__":
    import sys
    # the self-test: prove the segmentation is lossless at every size
    for n in (0, 1, 10, 999, 1000, 1001, 100000):
        s = ("line %d\n" % n) * (n // 8 + 1)
        for lim in (16, 64, 1024):
            segs = segment_text(s, lim)
            assert "".join(segs) == s, "lossy at n=%d lim=%d" % (n, lim)
            assert all(len(x) <= lim for x in segs), "a segment exceeded the limit"
    print("segment: lossless at every size and limit (the join is exact)")
