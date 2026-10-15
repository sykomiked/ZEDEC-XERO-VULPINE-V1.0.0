# Wave 24 task: Parallax (slot 294), Visual effects studio
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 3 (060 relief), wave 7 (056 cutwater), wave 13 (166 limner, 167 tintype), wave 15 (182 mummer).
Manifest: `slot 294`, `layer L3`, `icon icons/l3/294-parallax.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: a visual effects studio for layered images, motion and the depth effect, from sketch to finished shot, kept in the Hoard (`files.read`, `files.write`, `sys.disk`).
Uses: `use limner 1` (nearest palette colour, "may this AI stroke be applied"); `use tintype 1` (exposure value, "possibly altered"); `use mummer 1` (cue start times); `use relief 1` (vertex transform, depth effect offsets); `use cutwater 1` (edit list valid, timecode). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `frame_ok` (each frame's render within the declared frame budget in ms and bytes) in Ada as Eve + APL as KRON witnessed; `render` (layer compositing over integer alpha, clamped 0..255) in FORTRAN as Trebago + APL as KRON witnessed; `budget` (production budget per shot in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `pixels` arrays in APL as KRON; `shots` hierarchy in MUMPS as MYRA.
BOTH test: artists A and B both edit layer 4 of shot 12: the layer is BOTH and held for review; neither overwrites the other. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, sys.read, ai.ask.
Planned (no runtime yet): MATLAB.
Pending: `pending MATLAB render` (matrix).
