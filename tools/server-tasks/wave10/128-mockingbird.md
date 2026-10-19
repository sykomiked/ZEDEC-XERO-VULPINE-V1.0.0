# Wave 10 task: Mockingbird (slot 128), Hardware emulator
Read ../APP_WAVE_SPEC.md first.
What it does: emulate a ZXV device (the hwemu model) to try drivers and apps; machine facts from `sys.info`. zxvd has no emulator method: use only existing methods; list what's missing in your report.
Logic (critical): boot self-check passed (every stage's code 0 in order) in eve + kron witnessed; memory map regions do not overlap (sorted base and size vectors) in kron + trebago witnessed; cycle count per run, refused on overflow, in sutra + trebago witnessed; limits (RAM at most 4 GiB, at most 64 devices, at most 10^9 cycles) in eve; device record (kind, base, size, irq) in myra.
needs: sys.read.
Planned (no runtime yet): Simula 67, B, Racket.
