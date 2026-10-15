# Wave 8 task: Birdsong (slot 55), Voice memos and audio editing
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 55, build position 85 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: memos in the Hoard (`files.read`, `files.write`). Recording from the microphone has no method: use only existing methods; list what's missing in your report.
Logic (critical): gain and clip over sample arrays in kron + C++ wasm witnessed; clip length (samples to ms at a rate) exact in trebago; recording limits in eve; memo record in myra.
needs: mic, fs.read, fs.write.
Planned (no runtime yet): Mojo, LabVIEW, MATLAB.
