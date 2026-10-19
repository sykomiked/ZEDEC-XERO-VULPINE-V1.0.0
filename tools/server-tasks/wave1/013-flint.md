# Wave 1 task: Flint (slot 13), Terminal
Read ../APP_WAVE_SPEC.md first.
What it does: the terminal (zxsh, the Rosetta Shell; docs/ROSETTA_SHELL.md, tools/zxsh). The model runs commands through what zxsh already exposes; no new shell.
Logic: scrollback limits (lines, bytes) in eve; column/row fit for a window size (cells = pixels / cell size) in kron + C++ wasm witnessed; command history record in myra.
Text work (parsing command lines) stays C: its best-fit languages (AWK, Bash) have no runtime yet; say so in the manifest.
needs: sys.read, fs.read.
