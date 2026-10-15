# Wave 9 task: Spyglass (slot 117), Screen magnifier
Read ../APP_WAVE_SPEC.md first.
What it does: magnify the area around the pointer or focus, full screen or lens. Screen size from `sys.info`; zxvd has no display-capture or accessibility-settings method: use only existing methods; list what's missing in your report.
Logic (critical): source rectangle for a pointer and zoom (q8, 1x..16x, kept inside the screen) in kron + trebago witnessed; focus-follow step (at most N pixels a frame toward the target) in kron.
needs: sys.read.
Planned (no runtime yet): Dart (SDK host only), Laning and Zierler, Raku.
