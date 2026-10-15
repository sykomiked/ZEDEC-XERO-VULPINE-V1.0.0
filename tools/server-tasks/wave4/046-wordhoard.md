# Wave 4 task: Wordhoard (slot 46), Dictionary and thesaurus
Read ../APP_WAVE_SPEC.md first.
What it does: word lists kept in the Hoard (`files.read`, `search.query`), the language of a word (`xlate.detect`, `xlate.languages`); a definition from the model (`swarm.ask`) is held until the owner accepts it.
Logic: headword index and prefix lookup in myra; "did you mean" edit distance over code points in C++ wasm; synonym order by shared senses in kron.
needs: fs.read, search, ai.ask.
Planned (no runtime yet): AWK, TeX, PostScript.
