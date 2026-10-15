# Wave 8 task: Wellspring (slot 89), Health and fitness log
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 89, build position 90 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: a private log of activity, sleep and water kept in the Banker (`files.read`, `files.write`). No health method: use only existing methods; list what's missing in your report.
Logic (critical): "outside a safe range" (heart rate bpm, sleep minutes, water ml) in Rust wasm + eve witnessed; weekly totals and goal progress x100 exact in trebago; log entries in myra.
Advice only: never a diagnosis; out-of-range readings point to a clinician.
needs: fs.read, fs.write, notify.
Now runs (Lingua front-end built): Logo (twin Kiteling) for geometry: `logic logo <entry> apps/blocks/wellspring/<entry>.lg`, in the twin's canonical form, joined to another language's line for the same entry.
