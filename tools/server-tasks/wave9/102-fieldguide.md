# Wave 9 task: Fieldguide (slot 102), Offline encyclopedia and nature guide
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 102, build position 103 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: articles in a Hoard folder found by `search.query` (`search.refresh`, `files.read`).
Logic (critical): "show a caution" (any matched species marked toxic or dangerous) in kron + myra witnessed; identification key, one question at a time, in basic; match ranking in kron; article index in myra.
Never says a plant or fungus is safe to eat.
needs: fs.read, search.
Planned (no runtime yet): Python, Logo.
