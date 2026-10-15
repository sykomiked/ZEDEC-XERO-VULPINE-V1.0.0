# Wave 9 task: Patience (slot 105), Card games
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 105, build position 102 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: solitaire card games; saved games in the Banker (`files.read`, `files.write`).
Logic (critical): "legal move" (rank one lower, alternating colour, empty column takes a king) in kron + eve witnessed; deal from a seed and score in kron; games in myra.
No stakes: nothing is wagered or bought.
needs: fs.read, fs.write.
Planned (no runtime yet): Python, Pascal, Logo.
