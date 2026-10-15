# Wave 28 task: Monochord (slot 343), Optics and spectroscopy
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 5 (070 retort); wave 12 (155 alembic); wave 13 (167 tintype); wave 14 (179 cupel); wave 19 (233 stratum).
Manifest: `slot 343`, `layer L3`, `icon icons/l3/343-monochord.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: runs optics and spectroscopy experiments: captures spectra, fits lines, logs runs and shares reproducible results (`files.read`, `files.write`, `search.query`, `aipi.put`, `web4.cid`). zxvd has no method for spectrometer readings: use only existing methods; list what's missing in your report.
Uses: `use cupel 1` (integer step, precision bounds); `use alembic 1` ("step allowed", result tables); `use tintype 1` (exposure value); `use stratum 1` (source agreement); `use retort 1` (run limits, run record). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `laser_ok` (laser class and exposure time inside the bench's declared limits, shutter interlock closed) in Ada as Eve + APL as KRON witnessed; `line_fit` (peak centre from the integer-weighted centroid of the counts) in FORTRAN as Trebago + APL as KRON witnessed; `grant_spend` (in minor units) in COBOL as Sutra; `spectra` arrays in APL as KRON; `samples` hierarchy in MUMPS as MYRA.
BOTH test: instrument 1 finds a line at 656.3 nm and instrument 2 at 656.9: it is BOTH and flagged as an anomaly for study. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Laser limits are advisory; the lab's laser safety officer's rules apply.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search, camera, net.
Planned (no runtime yet): LabVIEW, Julia, Haskell, ML.
Pending: `pending LabVIEW line_fit` (signal); `pending Julia line_fit` (matrix); `pending Haskell laser_ok` (proof); `pending ML laser_ok` (proof).
