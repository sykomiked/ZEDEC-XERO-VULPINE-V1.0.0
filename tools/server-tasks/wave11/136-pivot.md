# Wave 11 task: Pivot (slot 136), Switch and eye-tracking control
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 136, build position 124 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: drive the machine with one or two switches (scanning) or the eyes (dwell to click). zxvd has no input-device or gaze method: use only existing methods; list what's missing in your report.
Logic (critical): dwell select (every gaze sample inside the target for at least the dwell time) in eve + kron witnessed; limits (dwell 200..5000 ms, scan step 300..10000 ms, target at least 44 px) in eve; profile record (switches, dwell, scan order) in myra.
A dwell never confirms a held action (pay, grant, trash); those need a second dwell on a confirm target.
needs: camera, sys.read.
Planned (no runtime yet): R, OCaml, Mojo.
