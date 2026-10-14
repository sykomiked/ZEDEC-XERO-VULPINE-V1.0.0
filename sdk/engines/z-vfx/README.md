<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Z-VFX: exact time, transforms, seeds and farm budgets for Maya and Houdini

Z-VFX is a Python package (`zxv_vfx`, Python 3.10+) for VFX pipelines. It
is built on the standard library and the [ZXV Python SDK](../../python/)
(`zxv`, which calls `libzxv`). It keeps the numbers a pipeline passes
between departments exact, so a shot set up, simulated or rendered on one
machine gives the same values on any other:

- **Frame time as rationals.** A frame rate is a rational (`24000/1001`,
  never `23.976`), a frame time is a rational frame number, and its time in
  seconds is exactly `frame / rate`. Retimes, subframes, motion-blur
  sample times and SMPTE timecode (drop-frame at 29.97 and 59.94 included)
  are exact integer and rational arithmetic. Nothing is ever float
  seconds.
- **Transforms as rationals.** `Vec3` and `Matrix4` hold `zxv.Rational`
  entries, multiplied and added in libzxv. Translation, scale, quarter
  turns and any rotation from a rational quaternion are exact; the inverse
  and determinant are exact; a matrix round-trips through text
  (`"1/3"`, `"12.5"`) for JSON or USD metadata. Overflow raises
  `ZXV_E_RANGE` instead of rounding.
- **Deterministic seeds.** A seed is SHA3-256 (from libzxv) of a
  versioned, canonical encoding of the namespace (show/sequence/shot) and
  the parts you name (asset path, frame time, instance index). A seed
  stream draws unbiased ints and exact rationals from
  `SHA3-256(seed || counter)` with rejection sampling. No host `random`,
  no `hash()`, no clock, no float.
- **Render-farm budgets.** `FarmBudget` holds the farm's allocation (in
  core-hours, or any unit you choose) as a libzxv `rmag` budget with an
  exact rational limit. A job reserves frames x cost per frame or is
  refused whole; completing it gives back an under-run or charges an
  over-run; cancelling gives back the frames not rendered.
- **Thin DCC adapters.** `zxv_vfx.maya` and `zxv_vfx.houdini` import
  `maya.cmds` and `hou` lazily, inside each call, and convert at the edge
  only.

Version 0.1.0 (preview): the API may change until 1.0.

## The float edge

The core modules (`frametime`, `transform`, `seeds`, `farm`, `_exact`)
contain no float. `tests/test_nofloat_ast.py` checks that on their syntax
trees: no float literal, no `float()` call, no `math`/`random` import, no
`**`, no `/` outside the one helper that divides two libzxv rationals, and
no import of the adapters. Every core entry point refuses a Python float
with `zxv.FloatRejectedError`.

Maya and Houdini take times, matrices and key values as doubles. The
adapters convert with one function, `zxv_vfx._edge.lossy_float` (the
nearest double; Python's `int / int` is correctly rounded), and every
function that does is marked **LOSSY** in its docstring. The conversion is
one-way: nothing reads a float from the DCC back into the core. The only
value read from a DCC is Maya's time unit, which is a string (`"film"`,
`"23.976fps"`) and maps to an exact rate. Houdini's `hou.fps()` is a float,
so the Houdini adapter reads nothing: the exact rate lives in your shot
record and is written to Houdini.

## Quickstart

```sh
make -C sdk/libzxv all                         # once, from the repository root
pip install -e sdk/python -e 'sdk/engines/z-vfx[test]'
export ZXV_LIBRARY_PATH=$PWD/sdk/libzxv/build
python sdk/engines/z-vfx/examples/plan_shot.py
```

```python
from zxv_vfx import FILM_NTSC, FrameRange, FrameTime, Matrix4, SeedScheme, Timecode

shot = FrameRange(1001, 1096, FILM_NTSC)
t = FrameTime(1001, FILM_NTSC)
t.seconds                                   # Rational(1002001, 24000)
t.retime(24).frame                          # Rational(1002001, 1000)
Timecode.from_frame(1800, "29.97")          # Timecode('00:01:00;02')
t.shutter_samples("-1/4", "1/4", 5)         # 1000.75, 1000.875, 1001, 1001.125, 1001.25

cam = Matrix4.from_quaternion(2, 0, 1, 0) @ Matrix4.translate("12.5", "1.7", -40)
cam.to_text()                               # exact strings, row by row

seeds = SeedScheme("showA/sq010/sh0040")
seeds.seed_int("fx/rbd/debris", 1001)       # 1909175317 on every machine

# inside Maya
from zxv_vfx import maya
maya.set_scene_rate(FILM_NTSC)              # currentUnit -time "23.976fps"
maya.set_playback_range(shot)               # LOSSY: Maya times are doubles
maya.set_world_matrix("|cam1", cam)         # LOSSY: xform -worldSpace -matrix
maya.set_int_attr("debrisSim", "seed", seeds.seed_int("fx/rbd/debris"))   # exact int

# inside Houdini
from zxv_vfx import houdini
houdini.set_scene_rate(FILM_NTSC)           # LOSSY: hou.setFps(23.976023976...)
houdini.set_world_transform("/obj/cam1", cam)
houdini.set_int_parm("/obj/debris/rbd", "seed", seeds.seed_int("fx/rbd/debris"))
```

`examples/plan_shot.py` prints a shot's frame range, timecodes, retime,
blur samples, seeds and farm reservations; two runs print the same bytes
(a test checks it).

## API

| Module | Names |
|---|---|
| `zxv_vfx.frametime` | `FrameRate` (`parse`, `of`, `frame_duration`, `nominal`, `drop_frames`, `is_ntsc`), `FrameTime` (`seconds`, `index`, `retime`, `timecode`, `shutter_samples`, `from_seconds`, `from_timecode`), `FrameRange`, `Timecode` (`parse`, `from_frame`, `to_frame`), constants `FILM`, `FILM_NTSC`, `PAL`, `NTSC`, `NTSC_30`, `SHOW`, `PAL_FIELD`, `NTSC_FIELD`, `HFR_60` |
| `zxv_vfx.transform` | `Vec3`, `Matrix4` (`identity`, `translate`, `scale`, `quarter_turns`, `from_quaternion`, `@`, `apply_point`, `apply_vector`, `inverse`, `determinant`, `transpose`, `to_text`/`from_text`, `flat`/`from_flat`) |
| `zxv_vfx.seeds` | `SeedScheme` (`seed`, `seed_int`, `stream`), `SeedStream` (`bits`, `below`, `randint`, `uniform`, `chance`, `choice`, `shuffle`, `sample`), `encode_part`, `DOMAIN` |
| `zxv_vfx.farm` | `FarmBudget` (`submit`, `complete`, `cancel`, `plan`, `raise_limit`, `remaining`, `used`), `RenderJob`, `Settlement`, `JobRefused` |
| `zxv_vfx.maya` | `set_scene_rate`, `scene_rate`, `maya_time_unit`, `rate_from_maya_unit`, `set_playback_range`, `set_current_time`, `set_world_matrix`, `set_keyframes`, `set_int_attr`, `RateMismatchError` |
| `zxv_vfx.houdini` | `set_scene_rate`, `set_playback_range`, `set_frame`, `set_world_transform`, `set_keyframes`, `set_int_parm` |

Conventions: transforms use row vectors (`p @ M`, translation in the last
row, `A @ B` applies A first), as Maya and Houdini do. Rounding a frame
time to a whole frame is always explicit: `floor`, `ceil`, `half_up` or
`half_even`.

## Tests

`pytest` in this folder: 99 tests against the real libzxv and the real
Python SDK (no mocks of either), with **fake** `maya.cmds` and `hou`
modules ([`tests/fakes`](tests/fakes)) that record calls and check that
times, matrices and values arrive as floats and seeds as ints.

| File | What it covers |
|---|---|
| `test_frametime.py` | rate parsing (NTSC labels mean N*1000/1001), float refusal, exact seconds with no drift over a million frames, subframe rounding, retimes there and back, shutter samples, ranges, 15 timecode vectors, a drop-frame round trip over every frame of 12 minutes and samples across a day, refused dropped labels |
| `test_transform.py` | composition order, right-handedness, rational quaternions (orthonormal and determinant 1, exactly), inverse, projective points, text round trip, exact associativity, overflow reported |
| `test_seeds.py` | seeds equal `hashlib.sha3_256` of the documented encoding, pinned vectors, stream blocks equal hashlib, uniformity, every draw, the same values in a fresh interpreter with a random `PYTHONHASHSEED` |
| `test_farm.py` | reservation and refusal, 3,000 one-third core-hour jobs filling a farm exactly, settling under- and over-runs, cancel, plan, the BUDGET capability |
| `test_adapters.py` | lazy imports, Maya unit names both ways, every adapter call against the fakes, rate mismatches refused, floats written to a DCC cannot re-enter the core |
| `test_nofloat_ast.py` | the core's syntax trees (above), and the checker itself on sources that must fail |
| `test_example.py` | the example runs and is byte-identical across two runs |

Run here on Linux x86-64 with Python 3.13 and 3.10: 99 passed, and
`mypy --strict` is clean. CI: the `z-vfx` job in
[`.github/workflows/sdk-engines.yml`](../../../.github/workflows/sdk-engines.yml)
(ubuntu-24.04, Python 3.10).

**Not run inside Maya or Houdini.** The adapters were tested only against
the fakes. The fakes follow the documented `maya.cmds` and `hou` calls, but
they cannot show that a given Maya or Houdini version accepts every flag
and argument, or how it rounds what it stores. Try the adapters in your
DCC before relying on them.

## Limits

- **Exactness stops at the DCC.** Once a value is written to Maya or
  Houdini it is a double, and the DCC computes with doubles from then on
  (evaluation, simulation, rendering). Z-VFX makes the values a pipeline
  hands over exact and reproducible; it does not make the DCC's own
  arithmetic deterministic.
- **Rotations are exact only when rational.** An angle in degrees has no
  exact rational rotation in general. Use quarter turns or rational
  quaternions in the core; leave arbitrary Euler angles to the DCC.
- **int64 rationals.** Values are `zxv.Rational` (int64 numerator and
  denominator). Long chains with many distinct denominators can overflow;
  that raises `ZXV_E_RANGE` rather than rounding. Keep values on a common
  grid (thousandths, the frame rate's denominator).
- **Timecode** covers one day (00:00:00:00 to 23:59:59:FF) at whole and
  N*1000/1001 rates, with drop-frame only at 29.97 and 59.94 (and their
  multiples). Frames past a day are refused, not wrapped.
- **Seeds are reproducible, not secret.** Anyone with the namespace and
  parts can recompute a seed; that is the point. Do not use them as keys.
  Changing the encoding would change every seed, so it is versioned
  (`zxv-vfx/seed/v1`).
- **The farm budget is an in-process ledger.** It does not talk to a
  render manager (Deadline, Tractor, OpenCue), measure real usage or
  survive the process; feed it your estimates and actuals and keep its
  totals in your own records. One `FarmBudget` is not shared across
  processes.
- **Maya time units** are the documented names (`film`, `pal`, `ntsc`,
  `23.976fps`, `120fps` ...). A unit Maya does not have (such as 12.5 fps)
  is refused.

## Files

| Path | What |
|---|---|
| [`src/zxv_vfx/`](src/zxv_vfx/) | the package: `frametime.py`, `transform.py`, `seeds.py`, `farm.py`, `_exact.py` (core); `maya.py`, `houdini.py`, `_edge.py` (the edge) |
| [`tests/`](tests/) | the tests and the fake DCC modules |
| [`examples/plan_shot.py`](examples/plan_shot.py) | the example |

## Licence

Apache-2.0, like the rest of the SDK.
