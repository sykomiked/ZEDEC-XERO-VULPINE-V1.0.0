# Wave 3 task: Footlight (slot 41), Presentations
Read ../APP_WAVE_SPEC.md first.
What it does: decks as files (`files.read`, `files.write`), printed handouts (`print.submit`), presented to others in a watch hall (`pspace.create`, `hall.load`, `hall.seek`, `hall.status`).
Logic (critical): slide scale to fit the window (16:9 into any size, letterbox) in kron + C++ wasm witnessed; rehearsal timing (seconds per slide, total against talk length) exact in trebago; run order (advance, hold, loop) in basic; slide record in myra.
needs: fs.read, fs.write, print, peers.
Planned (no runtime yet): Icon, TeX, PostScript.
