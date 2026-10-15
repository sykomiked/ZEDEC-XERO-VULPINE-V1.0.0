# Wave 6 task: Wordhoard (slot 46), Dictionary and thesaurus
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 46, build position 69 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: word lists kept in the Hoard (`files.read`, `search.query`), the language of a word (`xlate.detect`, `xlate.languages`); a definition from the model (`swarm.ask`) is held until the owner accepts it.
Logic: headword index and prefix lookup in myra; "did you mean" edit distance over code points in C++ wasm; synonym order by shared senses in kron.
needs: fs.read, search, ai.ask.
Planned (no runtime yet): AWK, TeX, PostScript.
Data (docs/DATAPACKS.md): the core pack `wordhoard-core.tar` (Open English WordNet 2024, CC BY 4.0, plus the Open Multilingual Wordnet lexicons whose own licence is TRUE) installs into the Hoard; Wiktionary (CC BY-SA) is the separate optional pack `wordhoard-optional.tar`. Read whichever is installed; show `ATTRIBUTION.txt`; never mix share-alike text into the app's code.
