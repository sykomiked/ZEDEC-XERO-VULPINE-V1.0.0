# Wave 9 task: Riddle (slot 104), Puzzles
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 104, build position 101 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: logic and number puzzles; saves in the Banker (`files.read`, `files.write`); hints from `swarm.ask`.
Logic (critical): "solved" (every row, column and box holds each value once) in kron + basic witnessed; daily puzzle pick from the date in kron; puzzles and times in myra.
needs: fs.read, fs.write, ai.ask.
Planned (no runtime yet): Mojo, Prolog.
