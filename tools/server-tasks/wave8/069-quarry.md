# Wave 8 task: Quarry (slot 69), Research
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 69, build position 91 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: search the Hoard (`search.query`, `files.read`), the owner's Web4 feed (`web4.feed`), summarize with the companion (`swarm.ask`). Searching the web has no method: use only existing methods; list what's missing in your report.
Logic (critical): "this claim has a source" (each claim names a held citation) in eve + kron witnessed; sources per topic in kron + trebago; summary length limits in eve; citation record in myra.
needs: search, fs.read, ai.ask, net.
Planned (no runtime yet): dBase, Python.
