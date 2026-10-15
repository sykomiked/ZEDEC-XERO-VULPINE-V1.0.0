# Wave 3 task: Satchel (slot 7), Packager
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 7, build position 32 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Witness (layer 18, wave 1, an earlier wave: `use witness 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 1 other layer lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: build, sign and install ZXV packages (`zxpkg`, AIPI signed bundles).
Logic: package size and file-count limits in eve; total size exact in trebago; per-file share of the package in kron + Rust wasm witnessed; package record in myra.
needs: fs.read, fs.write.
