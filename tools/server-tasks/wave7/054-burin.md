# Wave 7 task: Burin (slot 54), Vector drawing and engraving-style art
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 54, build position 74 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: drawings as files (`files.read`, `files.write`), printed (`print.submit`).
Logic (critical): curve flattening (de Casteljau in fixed point) in C++ wasm + kron witnessed; hatch lines across a shape (count and spacing) in kron + trebago; path limits in eve; path record in myra.
needs: fs.read, fs.write, print.
Planned (no runtime yet): APT, Logo, LabVIEW.
