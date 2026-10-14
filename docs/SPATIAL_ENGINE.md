# Spatial engine (roadmap S): exact geometry, fusion and fleet planning

The spatial engine is a small, freestanding C library in `kernel/src/spatial/` (prefix `sp_`).
It plans civilian robot and drone work: delivery and logistics, mapping and survey, infrastructure
inspection, disaster response and search-and-rescue support, and agriculture monitoring. Those
five purposes are a closed enum. Anything else is refused with `SP_ERR_PURPOSE`.

The library has no actuation API. Every plan it produces is advisory and carries
`advisory = 1` and `requires_human_approval = 1`. The vehicle's own autopilot and safety layer
stay in charge, and a person approves each plan. The library contains no weapons, no targeting
and no person-following concepts.

## Roadmap steps (section 6.6d, track S)

| Step | Status | Where |
|---|---|---|
| S.1 Exact integer geometry | Built | `sp_geom.h/.c` |
| S.2 Four-valued sensor fusion | Built | `sp_fusion.h/.c` |
| S.3 ROS 2 bridge | **Not built yet** | none |
| S.4 Mesh coordination | Planning part built (allocation, deconfliction, mission checks). Payload-handoff records on the ledger are **not built yet** | `sp_fleet.h/.c` |
| S.5 Local RTK correction sharing over Vinea | **Not built yet** | none |
| S.6 Eve pillar (Ada/SPARK geodesy) and geofences feeding S.1–S.5 | **Not built yet** (Wave 6) | none |

## Data model

- **Frame.** Positions are in a local Cartesian frame, in whole millimetres stored as `int64`
  (x east, y north, z up), with |coordinate| <= 2^29 mm (about 537 km). Times are in whole ms
  after a mission epoch, in the range 0 to 2^31 ms (about 24.8 days). Larger inputs are refused
  with `SP_ERR_RANGE`.
- **Geodetic input.** Latitude and longitude are stored exactly in micro-degrees.
  `sp_geo_to_local` projects them using scale factors (mm per micro-degree, as fractions) that
  the **caller** supplies. The longitude scale depends on cos(latitude), which is irrational, so
  this projection is **not exact**. The caller computes it at the edge, in floating point if
  needed.
- **Polygons.** A polygon has up to 32 vertices and must be simple. `sp_poly_validate` rejects
  self-intersection, zero-length edges, edges that fold back and zero area.
- **Mission.** A mission has:
  - a purpose;
  - a keep-in operating area;
  - an altitude band;
  - a clearance;
  - an energy budget in mWh (Wh × 1000);
  - up to 8 zones. A zone is either keep-out or protected habitat. A protected-habitat zone has
    an active window, such as a nesting season, plus a noise buffer and a noise ceiling.
- **Vehicle.** A vehicle record holds its class, the purposes it is declared for, its home
  position, payload limit, usable energy, a linear energy model (per km, per metre climbed, per
  hour), cruise speed, declared noise level and priority.
- **Path.** A path has 2 to 16 waypoints (x, y, z, t), with strictly increasing times and
  straight-line motion between waypoints.

## Algorithms (all exact integer arithmetic)

- **Predicates.** The orientation test is the sign of an int64 cross product. Segment
  intersection returns NONE, PROPER or TOUCH. Point in polygon returns IN, OUT or **ON**, and
  safety checks treat ON the cautious way. Distance comparisons against a margin use
  `cross^2 <= margin^2 * len^2`, computed in a 128-bit unsigned helper built from two `uint64`
  values. The library defines its own 64-bit division, square root and 64×64 multiply, so it needs
  no libgcc helpers on i386. A fixed 512-bit integer type (`sp_big`) handles deconfliction.
  Exact planar rotations use Pythagorean headings (a, b, c with a²+b²=c²) and return homogeneous
  coordinates, so rotated points are not rounded.
- **Nature boundaries are hard limits.** `sp_path_check` refuses a path if any of these hold:
  - it leaves the keep-in area or touches its boundary;
  - it comes closer than the clearance to an active keep-out or protected zone;
  - it passes within a protected zone's noise buffer while the zone is active and the vehicle is
    louder than the zone's ceiling;
  - it would use more energy than the vehicle or the mission allows.

  A refusal is final. These limits are never priced and cannot be bought past. A delay that
  moves a path into a nesting season is refused in the same way.
- **Fusion (S.2).** Each healthy source gives the closed interval [value − tol, value + tol].
  - If the intervals share a point, the state is TRUE, and the fused interval is their
    intersection.
  - If they do not, the state is BOTH. There is no voting or averaging. The rule uses the
    smallest lower bound as the obstacle distance, widens the margin and caps speed, or holds.
  - If there are too few healthy sources, the state is NEITHER, and the advice is HOLD.
  - FALSE is reserved and never produced.

  A faulted or degraded source rules out PROCEED. The enum values match `lpres_state_t`.
- **Allocation (S.4a).** Each vehicle takes one task.
  - A pair is feasible when the vehicle is declared for the task's purpose, the payload fits, the
    round trip's energy fits, and the route passes the mission check if a mission is given.
  - Cost = w_dist × metres + w_energy × mWh.
  - An exact integer Hungarian algorithm runs on a padded square matrix. Infeasible and dummy
    entries cost BIG > n × max cost. The result first maximises the number of assigned tasks,
    then minimises total cost.
  - Each unassigned task is reported with one reason: PURPOSE, PAYLOAD, ENERGY, ROUTE or
    OUTNUMBERED.
- **Deconfliction (S.4b).**
  - Separation is lost when the 3D distance is **strictly** less than `sep_mm`. Exactly
    `sep_mm` is not a loss.
  - For two segments whose time spans overlap, the scaled relative position is P + Q·s. The
    minimum of |P + Q·s|² over the overlap is compared exactly with sep², at the start of the
    overlap, at its end or at the interior minimum (a·c − b² vs a·sep²·D²). The arithmetic is
    512-bit and uses no floats.
  - The plan keeps higher-priority vehicles on schedule. It delays lower-priority departures in
    whole steps, re-running the full mission check at each step.
  - If no conflict-free delay exists within `max_steps`, the vehicle is UNRESOLVED and is not
    scheduled.

## Honest limits

- Exact arithmetic removes **arithmetic** drift only. Sensor drift (IMU, odometry, GPS) remains
  and still needs normal filtering and ground truth.
- The flat local frame and the caller's projection are approximations. Over long distances a real
  geodesy layer is needed (S.6).
- Fusion agreement only means the sources are consistent with the tolerances they claim. Two
  sources that are wrong in the same way still agree.
- Motion between waypoints is assumed straight-line. The model leaves out wind, GNSS error and the
  autopilot's path-following error, so `sep_mm` and `clearance_mm` must include those buffers.
- Zones are vertical columns. The energy model is linear and ignores payload mass, wind and
  battery ageing.
- The greedy delay plan is not optimal.
- Coordination runs at human timescales only. Nothing here is real-time control, and nothing is
  certified. The design is meant to support a human-approved, advisory workflow.

## Tests

From `kernel/`:

```
gcc -std=c11 -O1 -g -Wall -Werror -Wextra -Wdouble-promotion -Wfloat-conversion \
  -fsanitize=address,undefined -fno-sanitize-recover=all -DTEST_HOST -Iinclude \
  src/spatial/test_spatial.c src/spatial/sp_*.c -o $ZXV_TMP/test_spatial -lm \
  && $ZXV_TMP/test_spatial
```

`test_spatial.c` covers:
- wide arithmetic against native and `__int128` references;
- geometry edge cases: collinear, touching, coordinates at ±2^29 and overflow or range refusal;
- point in polygon (IN, OUT and ON, on convex and concave shapes);
- the full fusion truth table;
- the Hungarian algorithm and allocation, checked against brute force on random instances with
  fixed seeds;
- deconfliction: head-on, crossing, parallel and time-separated cases, plus the exact boundary at
  the separation distance;
- refusals for the protected-zone season and the energy budget;
- every error path.

The only floating-point code is in the test file, where it serves as an independent reference.
