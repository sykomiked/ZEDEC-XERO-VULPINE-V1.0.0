# Wave 1 task: Cellar (slot 19), Disk utility
Read ../APP_WAVE_SPEC.md first.
What it does: volumes, space by folder and space, Trash size, check and repair the Hoard, disc images (`sys.disk`, `files.*`).
Logic: used share from the used_share block (`use used_share 1`); folder shares in kron + C++ wasm witnessed (copy the Banker's share pattern); Trash total exact in trebago; "safe to repair" limits in eve.
needs: sys.read, fs.read, fs.write.
