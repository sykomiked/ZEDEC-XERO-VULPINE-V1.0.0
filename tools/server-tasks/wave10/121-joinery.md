# Wave 10 task: Joinery (slot 121), Code editor
Read ../APP_WAVE_SPEC.md first.
What it does: edit code in the Hoard (`files.read`, `files.write` begin/add/end, `files.rename`), find in files (`search.query`, `search.refresh`), run a Playground template (`play.run`), suggestions from `swarm.ask` held until the person accepts them.
Logic (critical): line and column to byte offset and back (UTF-8 widths as integer vectors) in kron + trebago witnessed; buffer limits (file at most 16 MiB, line at most 65536 bytes, undo at most 1024 steps) in eve; undo record (step, offset, removed length, inserted length) in myra.
COBOL owns no decision in an editor; it is left out. Parsing source text stays C (its best-fit languages have no runtime yet); say so in the manifest.
needs: fs.read, fs.write, search, ai.ask.
Planned (no runtime yet): Racket, Haskell, Bourne shell.
