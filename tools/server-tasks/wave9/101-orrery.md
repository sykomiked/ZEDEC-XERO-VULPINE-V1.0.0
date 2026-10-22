# Wave 9 task: Orrery (slot 101), Sky, stars and planets
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 101, build position 98 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: the sky from where the machine is (`place.here`, `sys.info` for the clock). No sky data method: the block holds the integer tables.
Logic (critical): day number from the date and mean longitude per planet (millidegrees, mod 360000) in kron + fortran77 witnessed; "above the horizon" in kron; observations in myra.
needs: location, sys.read.
Now runs (Lingua front-end built): Pascal (twin Tutordrake) for step-by-step teaching: `logic pascal <entry> apps/blocks/orrery/<entry>.pas`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): Mojo, Simula I.
