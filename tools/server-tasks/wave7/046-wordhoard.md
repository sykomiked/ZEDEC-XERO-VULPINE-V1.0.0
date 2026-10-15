# Wave 7 task: Wordhoard (slot 46), Dictionary and thesaurus
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 46, build position 80 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: word lists kept in the Hoard (`files.read`, `search.query`), the language of a word (`xlate.detect`, `xlate.languages`); a definition from the model (`swarm.ask`) is held until the owner accepts it.
Logic: headword index and prefix lookup in myra; "did you mean" edit distance over code points in C++ wasm; synonym order by shared senses in kron.
needs: fs.read, search, ai.ask.
Planned (no runtime yet): AWK, TeX, PostScript.
