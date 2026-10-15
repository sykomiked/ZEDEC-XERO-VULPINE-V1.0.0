# Wave 7 task: Pinhole (slot 58), Camera
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 58, build position 78 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: photos saved in the Hoard (`files.write`), room left from `sys.disk`, a place tag only after the owner's answer (`place.ask`). Capturing from the camera has no method: use only existing methods; list what's missing in your report.
Logic (critical): device ready (sensor present, mode supported, buffer fits, as flags) in forth + eve witnessed; "photo fits" (bytes vs free) in sutra + kron witnessed; bytes per album exact in trebago; exposure limits in eve; photo record in myra.
The camera is shown as on whenever it is; the location is never added without the owner's permit.
needs: camera, location, fs.write, sys.read.
Planned (no runtime yet): LabVIEW, Julia.
