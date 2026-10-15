# Wave 3 task: Kinetoscope (slot 51), Video player
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 51, build position 28 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Witness (layer 18, wave 1, an earlier wave: `use witness 1`); the core (layer 0: reach it through zxvd methods that exist). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 1 other layer lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: videos from the Hoard (`files.read`), cut for streaming (`stream.plan`, `stream.segment`), watched together (`hall.load`, `hall.play`, `hall.pause`, `hall.seek`, `hall.status`). Local video output has no method: use only existing methods; list what's missing in your report.
Logic (critical): fit to the window (letterbox from width and height) in kron + C++ wasm witnessed; position to frame at rate num/den exact in trebago; rate limits (rate_q16 up to 4x) in eve; resume record in myra.
needs: fs.read, peers.
Planned (no runtime yet): LabVIEW, MATLAB.
