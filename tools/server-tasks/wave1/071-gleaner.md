# Wave 1 task: Gleaner (slot 71), Datasets
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 71, build position 7 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Talisman (layer 76, wave 1, an earlier wave: `use talisman 1`); Coffer (layer 73, wave 4, a later wave (a cycle): `use coffer 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 5 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: datasets as files in the Hoard (`files.list`, `files.read`, `files.write`, `search.query`), named by CID (`web4.cid`), published locally (`aipi.put`).
Logic (critical): train and test split (by row index, every row once) in kron + C++ wasm witnessed; label counts per class in kron + trebago witnessed; row filters over columns (label, source, date) as K queries in kron; size limits in eve; row record in myra.
A row with personal data never leaves the Hoard without the owner's consent.
needs: fs.read, fs.write, search, net.
Planned (no runtime yet): Python, Go.
