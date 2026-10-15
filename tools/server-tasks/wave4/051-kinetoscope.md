# Wave 4 task: Kinetoscope (slot 51), Video player
Read ../APP_WAVE_SPEC.md first.
What it does: videos from the Hoard (`files.read`), cut for streaming (`stream.plan`, `stream.segment`), watched together (`hall.load`, `hall.play`, `hall.pause`, `hall.seek`, `hall.status`). Local video output has no method: use only existing methods; list what's missing in your report.
Logic (critical): fit to the window (letterbox from width and height) in kron + C++ wasm witnessed; position to frame at rate num/den exact in trebago; rate limits (rate_q16 up to 4x) in eve; resume record in myra.
needs: fs.read, peers.
Planned (no runtime yet): LabVIEW, MATLAB.
