# Wave 5 task: Flint (slot 13), Terminal
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 13, build position 49 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. Report the reuse line `concepts.py` prints for this app.
What it does: the terminal (zxsh, the Rosetta Shell; docs/ROSETTA_SHELL.md, tools/zxsh). The model runs commands through what zxsh already exposes; no new shell.
Logic: scrollback limits (lines, bytes) in eve; column/row fit for a window size (cells = pixels / cell size) in kron + C++ wasm witnessed; command history record in myra.
Text work (parsing command lines) stays C: its best-fit languages (AWK, Bash) have no runtime yet; say so in the manifest.
needs: sys.read, fs.read.
