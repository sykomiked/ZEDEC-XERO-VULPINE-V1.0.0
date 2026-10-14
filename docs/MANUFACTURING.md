# Manufacturing track (roadmap F): toolpath checker, inspection, nature budgets

Code: `kernel/src/mfg/` (prefix `mfg_`). Status: F.2, F.4 and F.10 are built,
plus a minimal admission queue for F.6. Everything else in track F is a
later step (table below).

## Ground rules

- **Read-only and advisory.** Nothing in `mfg_` talks to a machine, sends a
  program, writes a setpoint or starts a job. The machine's own certified
  safety system (e-stops, interlocks, PLC safety) stays in charge, and a
  person approves every release. Machine writes come after F.1-F.4, and only
  with the owner's approval.
- No AI in any machine control loop. No weapons or munitions manufacturing.
- No IEC 61508 or IEC 62443 compliance claims. The code is designed to
  support a review process; it does not certify anything.
- Nature's boundaries are hard budgets: a breach makes the job wait; it is
  never priced, traded or bought past.
- Four-valued logic, same numbering as `lpres_state_t` (NEITHER=0, TRUE=1,
  FALSE=2, BOTH=3; `mfg_tv_t` is a local copy). Missing evidence is NEITHER
  and holds; conflicting evidence is BOTH and holds for a re-measure or a
  person. Neither is resolved by guessing.

## Steps

| Step | What | Status |
|---|---|---|
| F.1 | Read-only OPC UA / MQTT / Modbus-TCP adapters into zxvd, signed and logged | Later |
| F.2 | Toolpath checker for G-code (`mfg_gcode`) | **Built** (G-code subset; robot programs later) |
| F.3 | Exact digital twin of a cell on the S.1 geometry | Later |
| F.4 | Four-valued, guard-banded inspection (`mfg_qc`) | **Built** |
| F.5 | Predictive maintenance on machine time series, as advice | Later |
| F.6 | Scheduling: jobs bid for machine time, no monopoly (`mfg_queue_*`) | **Minimal admission queue built**; `provider`/`capmkt` link later |
| F.7 | Traceability ledger and buyer exports | Later |
| F.8 | Machine-time marketplace between owners | Later |
| F.9 | Link to the 3D-printed electronics line | Later |
| F.10 | Nature boundaries as hard budgets (`mfg_site_*`) | **Built** |
| F.11 | Circular flows in the traceability ledger | Later |
| F.12 | Regenerative share to verified restoration | Later |

## F.2 toolpath checker (`mfg_gcode.h`)

Input: program text in a caller buffer, a machine profile, a caller-owned
result. Output: up to 64 issues (line, N word, code, severity, message; more
are counted in `dropped`) and a verdict RELEASE_OK / HOLD / REJECT.

- **Exact numbers.** Every number is parsed from its decimal text into
  integer micrometres (1 mm = 1000 um, 1 in = 25400 um exactly). A value that
  is not a whole number of um (0.0001 mm, 0.001 in = 25.4 um) is rejected,
  never rounded. Feeds are um/min, spindle speeds whole rpm.
- **Words.** N, G0-G3, G17, G20/G21, G90/G91, X Y Z I J F S, M3/M4/M5,
  M2/M30, `( )` and `;` comments, a `%` line. Anything else (G18, G28, G41,
  T, R ...) is UNKNOWN_WORD and rejects the job.
- **Checks.** Travel envelope; feed set, within [feed_min_cut, feed_max] for
  G1/G2/G3; spindle range on M3/M4 and on S changes while on; cutting with
  the spindle off; rapids that move sideways below safe Z or descend below
  it (a straight retract upward is allowed); rapids touching keep-out boxes;
  any move touching a fixture box; arc centre offset present and non-zero;
  start and end radius^2 within `arc_r2_tol`; modal state (motion before
  G20/G21 and G90/G91 holds; axis words with no motion mode reject); missing
  M2/M30 and words after it hold.
- **Segment vs box.** Closed axis-aligned boxes, exact slab test with the
  parameter t kept as a rational num/den and compared by cross
  multiplication. Touching a face, an edge or a corner counts as a hit.
  Coordinates are bounded to +-500 m so every product fits in int64.
- **Arcs.** The swept angle is decided exactly from integer cross and dot
  products (which of +X, +Y, -X, -Y the arc passes). The radius uses an
  exact integer square root: floor for the smallest possible extent, ceil
  for the largest. Each extreme point is known to lie in a tiny candidate
  box; if a fixture contains it whole (or contains the end point) the
  collision is definite (REJECT). If only the conservative arc bounding box
  touches a fixture, or leaves the envelope, the result is BOTH and the job
  is held for a person (HOLD), never passed.

Honest limits: the tool is a point (inflate fixture boxes by tool radius and
holder yourself); no cutter compensation, work offsets, canned cycles,
R-form arcs or other planes; feeds are the programmed values, not real
acceleration or overrides; robot programs are not parsed yet. RELEASE_OK
means "these checks found nothing", not "safe".

## F.4 inspection (`mfg_qc.h`)

Per feature: nominal and tolerance band `[nominal - tol_minus, nominal +
tol_plus]`; up to 8 gauges, each with value v, uncertainty u (integer um) and
a valid flag. A gauge is IN only if `[v-u, v+u]` lies inside the closed band,
OUT only if it lies entirely outside, otherwise ambiguous (touching a limit
from outside is ambiguous). A feature is TRUE if all valid gauges are IN,
FALSE if all are OUT, BOTH on any disagreement or ambiguity, NEITHER with no
valid gauge. A part passes only if every feature is TRUE; any FALSE feature
rejects it; otherwise it is held. Limit: the decision trusts the uncertainty
the caller gives; no gauge or camera is read here.

## F.10 nature budgets and the F.6 queue (`mfg_budget.h`)

A site has, per epoch, hard limits for energy (Wh), water (mL), material
(g), waste (g) and emissions (g CO2e). Use comes only from meter readings of
cumulative use in the epoch. Meter state: NEITHER (none yet or offline),
TRUE (reading held), BOTH (a reading went backwards; stays BOTH until a
person clears it). A job is admitted only if, for every resource it needs,
the meter is TRUE and `measured + reserved + need <= limit` (exactly at the
limit is allowed). Admission reserves the need; `mfg_site_release` drops the
reservation once the meter shows the job's use. The check takes no price or
bid, so no bid can buy past a boundary.

The queue admits, per epoch, the highest-bid job (ties: earlier
submission) that passes the budget and fits the remaining machine minutes.
No-monopoly rule: an owner may not go above `share_num/share_den` (default
1/3) of the epoch's machine-time capacity while another job within its own
owner's share is admissible. The share is measured against capacity, not
against the running admitted total (that would block every owner's first
job). If every admissible job is over its owner's share, the highest bid
among them is admitted so the machine does not sit idle. Limits: limits come
from the operator (published ecological thresholds, local permits); meter
readings are trusted as given (signing belongs to F.1); no payment or
settlement happens here.

## Tests

From `kernel/`:

    gcc -std=c11 -O1 -g -Wall -Werror -Wextra -Wdouble-promotion -Wfloat-conversion \
        -fsanitize=address,undefined -fno-sanitize-recover=all -DTEST_HOST \
        -Iinclude src/mfg/test_mfg.c src/mfg/mfg_*.c -o /tmp/test_mfg -lm && /tmp/test_mfg

The tests cover exact decimal and inch parsing, every checker issue with its
line number, a clean program, malformed input, line/size/issue limits, arc
direction, extremes, tolerance and HOLD cases, segment/box edge cases (face,
edge, corner, parallel, rational entry points, plus a grid property test),
the full QC truth table with exact boundary values and a brute-force
property test, budget admission edges (exactly at the limit, one over,
missing and contradictory meters, high bids that still wait) and the
monopoly cap. Module code is freestanding (integer only, no allocation, no
libc) and builds for aarch64 and i386 with only `mfg_` symbols.
