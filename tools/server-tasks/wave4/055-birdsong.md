# Wave 4 task: Birdsong (slot 55), Voice memos and audio editing
Read ../APP_WAVE_SPEC.md first.
What it does: memos in the Hoard (`files.read`, `files.write`). Recording from the microphone has no method: use only existing methods; list what's missing in your report.
Logic (critical): gain and clip over sample arrays in kron + C++ wasm witnessed; clip length (samples to ms at a rate) exact in trebago; recording limits in eve; memo record in myra.
needs: mic, fs.read, fs.write.
Planned (no runtime yet): Mojo, LabVIEW, MATLAB.
