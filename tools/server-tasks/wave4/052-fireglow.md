# Wave 4 task: Fireglow (slot 52), Podcasts and audiobooks
Read ../APP_WAVE_SPEC.md first.
What it does: episodes and books in the Hoard (`files.list`, `files.read`), positions saved (`files.write`). Fetching feeds has no method: use only existing methods; list what's missing in your report.
Logic (critical): time left at a speed (ms x 65536 / rate_q16) in trebago + kron witnessed; listening share per show (minutes listened x10000 / total) in kron + C++ wasm witnessed; chapter from position in kron; speed limits in eve; episode record in myra.
No money moves in this app.
needs: fs.read, fs.write, net.
Planned (no runtime yet): Raku.
