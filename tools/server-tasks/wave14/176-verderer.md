# Wave 14 task: Verderer (slot 176), Field survey
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (085 waymark), wave 8 (058 pinhole, 088 weatherglass), wave 9 (102 fieldguide).
Manifest: `slot 176`, `layer L2`, `icon icons/l2/176-verderer.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: logs wildlife sightings, trails and habitats with photos (`files.read`, `files.write`) and maps (`place.here`, `place.add`, `place.list`), searchable with `search.query`.
Uses: `use pinhole 1` (photo record, "photo fits"); `use waymark 1` (`waymark.place_ok`); `use weatherglass 1` (readings); `use fieldguide 1` ("show a caution", identification key, match ranking). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): population estimate (marked x caught / recaptured, floor, refused when none recaptured) in FORTRAN as Trebago + APL as KRON witnessed; "share this place" (a protected species' place is coarsened to the 10 km grid before it leaves the device) in Ada as Eve + APL as KRON witnessed; protected-area limits in Ada as Eve; sighting grid (species by grid cell) in APL as KRON; species tree in MUMPS as MYRA.
BOTH test: sighting 14 is identified as species 301 by the observer and 302 by the key: it is BOTH and kept until an expert confirms.
Never says a plant or fungus is safe to eat (Fieldguide's rule).
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: camera, location, fs.read, fs.write, search.
Planned (no runtime yet): MATLAB.
