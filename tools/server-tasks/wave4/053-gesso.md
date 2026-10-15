# Wave 4 task: Gesso (slot 53), Image editing and painting
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 53, build position 38 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Seedbank (layer 33, wave 1, built before this one: `use seedbank 1`); Talisman (layer 76, wave 1, built before this one: `use talisman 1`); Hollow (layer 2, wave 2, built before this one: `use hollow 1`). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. 1 other layer lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: images from the Hoard (`files.read`, base64); an edit is saved as a new file (`files.write`), never over the original until the owner says so.
Logic (critical): pixel operations over whole rows (brightness, contrast, blend, clamped 0..255) in kron + C++ wasm witnessed; histogram counts in kron + trebago; brush and canvas limits in eve; layer record in myra.
needs: fs.read, fs.write.
Planned (no runtime yet): Julia, LabVIEW.
