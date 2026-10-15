# Wave 10 task: Glimpse (slot 111), Screenshots and screen recording
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 111, build position 110 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: capture the screen, a window or a region, record the screen; save into the Hoard with `files.write` ("Screenshot N.png", first free N from `files.list`); cut recordings with `stream.plan`. zxvd has no display-capture method: use only existing methods; list what's missing in your report.
Logic (critical): capture region clamped to the screen (x, y, w, h inside width and height, never negative) in kron + trebago witnessed; recording size (w x h x bytes per pixel x frames, at most 7200 s and 60 fps) exact, refused past the free space `sys.disk` reports, in trebago + kron witnessed; first free N from the used numbers vector in kron.
A recording always shows that it is recording; nothing is captured in tests.
needs: fs.read, fs.write, sys.read, notify.
Planned (no runtime yet): Visual Basic (SDK host only), dBase, ALGOL 68.
