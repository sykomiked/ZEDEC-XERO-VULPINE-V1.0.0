# Wave 2 task: Hollow (slot 2), The desktop
Read ../APP_WAVE_SPEC.md first.
What it does: wallpaper (an image from the Hoard, `files.read`), the dock, windows and the space you are in (`spaces.list`, `spaces.get`); reports the app in front once a minute (`screen.tick`); notices (`notes.list`, `notes.mark_read`); simple mode (`ui.get`). Dock pins and window places are kept as one file in the Hoard (`files.write`).
Logic (critical): window tiling (columns, rows and cell size for a screen in pixels) in kron + C++ wasm witnessed; window limits (minimum size, at least 32 px on screen) in eve; dock order record in myra.
needs: sys.read, fs.read, fs.write, screen.time, notify.
Planned (no runtime yet): B, Elixir.
