# Wave 10 task: Spyglass (slot 117), Screen magnifier
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 117, build position 111 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: magnify the area around the pointer or focus, full screen or lens. Screen size from `sys.info`; zxvd has no display-capture or accessibility-settings method: use only existing methods; list what's missing in your report.
Logic (critical): source rectangle for a pointer and zoom (q8, 1x..16x, kept inside the screen) in kron + trebago witnessed; focus-follow step (at most N pixels a frame toward the target) in kron.
needs: sys.read.
Planned (no runtime yet): Dart (SDK host only), Laning and Zierler, Raku.
