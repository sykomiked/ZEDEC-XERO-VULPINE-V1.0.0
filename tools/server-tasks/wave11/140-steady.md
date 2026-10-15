# Wave 11 task: Steady (slot 140), Pointer and keyboard help
Read ../APP_WAVE_SPEC.md first.
What it does: sticky keys, slow keys, bounce keys, mouse keys. zxvd has no input-filter method: use only existing methods; list what's missing in your report.
Logic (critical): key accepted (held at least the slow delay AND not a repeat inside the bounce window) in eve + kron witnessed; sticky modifier step (off, latched, locked) in eve; limits (delays 0..2000 ms, mouse-key speed 1..50 px a tick) in eve; profile record in myra.
needs: sys.read.
Planned (no runtime yet): Racket, Mojo, AWK.
