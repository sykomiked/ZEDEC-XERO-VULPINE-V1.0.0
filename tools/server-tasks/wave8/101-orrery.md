# Wave 8 task: Orrery (slot 101), Sky, stars and planets
Read ../APP_WAVE_SPEC.md first.
What it does: the sky from where the machine is (`place.here`, `sys.info` for the clock). No sky data method: the block holds the integer tables.
Logic (critical): day number from the date and mean longitude per planet (millidegrees, mod 360000) in kron + fortran77 witnessed; "above the horizon" in kron; observations in myra.
needs: location, sys.read.
Planned (no runtime yet): Mojo, Simula I, Pascal.
