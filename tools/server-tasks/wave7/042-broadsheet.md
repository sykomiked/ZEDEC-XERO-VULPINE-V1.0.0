# Wave 7 task: Broadsheet (slot 42), Document viewer and annotation
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 42, build position 76 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: open documents from the Hoard (`files.read`, base64), print (`print.submit`); annotations go to a separate file beside the original (`files.write`), never into it.
Logic (critical): "well formed" (header present, every cross-reference offset inside the file size) in Rust wasm + eve witnessed, so a malformed file is refused; zoom to fit page in kron; annotation record in myra.
needs: fs.read, fs.write, print.
Planned (no runtime yet): Perl, PostScript, TeX.
