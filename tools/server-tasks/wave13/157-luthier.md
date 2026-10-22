# Wave 13 task: Luthier (slot 157), Music writing
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 6 (050 warble), wave 7 (055 birdsong, 057 fiddlehead).
Manifest: `slot 157`, `layer L2`, `icon icons/l2/157-luthier.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: composes, notates and records music, from instrument loops to finished scores, kept in the Hoard (`files.read`, `files.write`). zxvd has no method for audio capture and playback devices (as Birdsong reports): use only existing methods; list what's missing in your report.
Uses: `use warble 1` (playlist length, volume limits, track index); `use birdsong 1` (gain and clip over samples, clip length, recording limits); `use fiddlehead 1` (beat grid, pitch, tempo and pattern limits, loop record). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): royalty splits among contributors (largest remainder, summing exactly to the whole, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; a note's frequency in a chosen tuning (ratio num/den over the reference, milli-hertz, floor) in FORTRAN as Trebago + APL as KRON witnessed; level limits (track -600..+60 tenths of a dB, master never above 0) in Ada as Eve + APL as KRON witnessed; "bar is full" (note durations sum to the time signature's ticks) in APL as KRON + FORTRAN as Trebago witnessed; piece hierarchy in MUMPS as MYRA.
BOTH test: players A and B both edit bar 12 of part 2: the bar is BOTH and both versions are kept and can be heard.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, mic.
Planned (no runtime yet): LabVIEW.
