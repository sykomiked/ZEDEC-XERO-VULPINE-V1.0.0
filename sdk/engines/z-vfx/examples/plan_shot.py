# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Plan one shot with Z-VFX, outside any DCC.

A 23.976 fps shot: its frame range and timecodes, motion-blur sample
times, the per-instance seeds of a debris simulation, a camera transform
kept as exact text, and the shot's render jobs against a farm budget.
Every number printed is exact; run it twice, or on another machine, and
the output is identical.

    ZXV_LIBRARY_PATH=../../libzxv/build python examples/plan_shot.py
"""

from __future__ import annotations

import json

import zxv

from zxv_vfx import (FILM_NTSC, FarmBudget, FrameRange, FrameTime, JobRefused, Matrix4, RenderJob,
                     SeedScheme, Timecode)


def main() -> None:
    shot = FrameRange(1001, 1096, FILM_NTSC)
    first, last = FrameTime(shot.start, shot.rate), FrameTime(shot.end, shot.rate)
    record_in = Timecode.parse("01:00:00:00", FILM_NTSC).to_frame(FILM_NTSC)
    print(f"shot sh0040: frames {shot.start}-{shot.end} at {shot.rate} fps "
          f"({shot.count} frames, {shot.duration} s)")
    print(f"  frame {shot.start} is {first.seconds} s; record timecode "
          f"{Timecode.from_frame(record_in, FILM_NTSC)} .. "
          f"{Timecode.from_frame(record_in + shot.count - 1, FILM_NTSC)}")
    print(f"  on a 24 fps edit the last frame lands on frame {last.retime(24).frame} "
          f"(snapped: {last.retime(24, 'half_even').frame})")

    blur = first.shutter_samples("-1/4", "1/4", 5)
    print("  180-degree shutter samples:", ", ".join(str(s.frame) for s in blur))

    seeds = SeedScheme("showA/sq010/sh0040")
    print("  debris seeds (31-bit, any DCC):",
          [seeds.seed_int("fx/rbd/debris", i) for i in range(4)])
    st = seeds.stream("fx/rbd/debris", "scatter")
    print("  scatter offsets (exact, 1/1000 grid):",
          [str(st.uniform("-1/2", "1/2", 1000)) for _ in range(4)])

    cam = Matrix4.from_quaternion(2, 0, 1, 0) @ Matrix4.translate("12.5", "1.7", "-40")
    print("  camera world matrix (exact text):", json.dumps(cam.to_text()[3]))

    with zxv.Context() as ctx:
        cap = ctx.root_capability(zxv.Cap.BUDGET)
        with FarmBudget(cap, 400) as farm:  # core-hours this week
            jobs = [RenderJob("sh0040_fx_sim", FrameRange(1001, 1096, FILM_NTSC), "1/4"),
                    RenderJob("sh0040_beauty", shot, "2.5"),
                    RenderJob("sh0040_beauty_v2", shot, "2.5")]
            for j in jobs:
                try:
                    farm.submit(j)
                    print(f"  farm: {j.name} reserved {j.cost} core-hours, {farm.remaining} left")
                except JobRefused as e:
                    print(f"  farm: {j.name} refused: needs {e.needed}, {e.remaining} left")
            s = farm.complete("sh0040_fx_sim", "21.75")
            print(f"  farm: sim finished at {s.charged}, {s.returned} back, {farm.remaining} left")


if __name__ == "__main__":
    main()
