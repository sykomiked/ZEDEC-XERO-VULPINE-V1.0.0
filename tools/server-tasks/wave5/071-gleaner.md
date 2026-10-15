# Wave 5 task: Gleaner (slot 71), Datasets
Read ../APP_WAVE_SPEC.md first.
What it does: datasets as files in the Hoard (`files.list`, `files.read`, `files.write`, `search.query`), named by CID (`web4.cid`), published locally (`aipi.put`).
Logic (critical): train and test split (by row index, every row once) in kron + C++ wasm witnessed; label counts per class in kron + trebago witnessed; row filters over columns (label, source, date) as K queries in kron; size limits in eve; row record in myra.
A row with personal data never leaves the Hoard without the owner's consent.
needs: fs.read, fs.write, search, net.
Planned (no runtime yet): Python, Go.
