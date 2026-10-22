# Wave 10 task: Gridmark (slot 120), QR codes and barcodes
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 120, build position 114 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: make QR codes and EAN-13 barcodes, save them as images (`files.write`), read them from images in the Hoard (`files.read`) or the camera. zxvd has no image-decode method: use only existing methods; list what's missing in your report.
Logic (critical): QR version for a length and error level (capacity table) in kron + trebago witnessed; EAN-13 check digit in trebago + kron witnessed; Reed-Solomon error words over GF(256) (log/antilog tables) in kron + trebago witnessed; mask penalty per mask (four rules over the module grid) in kron.
A read link is shown, never opened without the person's tap.
needs: fs.read, fs.write, camera.
Now runs (Lingua front-end built): ALGOL 60 (twin Rootwyrm) for formulas: `logic algol60 <entry> apps/blocks/gridmark/<entry>.alg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): OCaml, AWK.
