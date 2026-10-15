# Wave 10 task: Twelvefold (slot 132), Paraconsistent logic workbench
Read ../APP_WAVE_SPEC.md first.
What it does: write formulas over TRUE, FALSE, BOTH, NEITHER, see their tables and the 64-state hexagram tag; try the engines' Playground templates with `play.run`; save formulas in the Hoard (`files.read`, `files.write`).
Logic (critical): evaluation stays C through `lpres_conjoin` / `lpres_disjoin` (kernel order, never re-derived; say so in the manifest); assignments per resulting value counted in kron; "conclusion follows" (every assignment where the premises are TRUE or BOTH gives the conclusion TRUE or BOTH, over the value codes C produced) in eve + kron witnessed; limits (at most 6 variables, depth at most 16) in eve.
needs: fs.read, fs.write.
Planned (no runtime yet): Lua, Clojure, Raku.
