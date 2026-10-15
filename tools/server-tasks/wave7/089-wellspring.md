# Wave 7 task: Wellspring (slot 89), Health and fitness log
Read ../APP_WAVE_SPEC.md first.
What it does: a private log of activity, sleep and water kept in the Banker (`files.read`, `files.write`). No health method: use only existing methods; list what's missing in your report.
Logic (critical): "outside a safe range" (heart rate bpm, sleep minutes, water ml) in Rust wasm + eve witnessed; weekly totals and goal progress x100 exact in trebago; log entries in myra.
Advice only: never a diagnosis; out-of-range readings point to a clinician.
needs: fs.read, fs.write, notify.
Planned (no runtime yet): Logo.
