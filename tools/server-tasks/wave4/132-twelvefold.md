# Wave 4 task: Twelvefold (slot 132), Paraconsistent logic workbench
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 132, build position 41 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 2 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: write formulas over TRUE, FALSE, BOTH, NEITHER, see their tables and the 64-state hexagram tag; try the engines' Playground templates with `play.run`; save formulas in the Hoard (`files.read`, `files.write`).
Logic (critical): evaluation stays C through `lpres_conjoin` / `lpres_disjoin` (kernel order, never re-derived; say so in the manifest); assignments per resulting value counted in kron; "conclusion follows" (every assignment where the premises are TRUE or BOTH gives the conclusion TRUE or BOTH, over the value codes C produced) in eve + kron witnessed; limits (at most 6 variables, depth at most 16) in eve.
needs: fs.read, fs.write.
Planned (no runtime yet): Lua, Clojure, Raku.
