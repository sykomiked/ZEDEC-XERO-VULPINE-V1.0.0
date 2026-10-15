# Wave 4 task: Cutwater (slot 56), Video editing
Read ../APP_WAVE_SPEC.md first.
What it does: sources from the Hoard (`files.read`), cut points checked with `stream.plan`, the edit list saved (`files.write`). Rendering a finished video has no method: use only existing methods; list what's missing in your report.
Logic (critical): edit list valid (in before out, inside the source, no overlap on a track) in eve + kron witnessed; timecode (frames at rate num/den to h:m:s:f) in trebago + kron witnessed; clip record in myra.
needs: fs.read, fs.write.
Planned (no runtime yet): LabVIEW, Julia.
