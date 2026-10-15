# Wave 1 task: Satchel (slot 7), Packager
Read ../APP_WAVE_SPEC.md first.
What it does: build, sign and install ZXV packages (`zxpkg`, AIPI signed bundles).
Logic: package size and file-count limits in eve; total size exact in trebago; per-file share of the package in kron + Rust wasm witnessed; package record in myra.
needs: fs.read, fs.write.
