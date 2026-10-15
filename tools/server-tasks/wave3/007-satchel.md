# Wave 3 task: Satchel (slot 7), Packager
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 7, build position 32 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Witness (layer 18, wave 1, built before this one: `use witness 1`). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. 1 other layer lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: build, sign and install ZXV packages (`zxpkg`, AIPI signed bundles).
Logic: package size and file-count limits in eve; total size exact in trebago; per-file share of the package in kron + Rust wasm witnessed; package record in myra.
needs: fs.read, fs.write.
