# Wave 8 task: Typewright (slot 99), Typing tutor
Read ../APP_WAVE_SPEC.md first.
What it does: typing lessons and drills; lessons and results in the Banker (`files.read`, `files.write`).
Logic (critical): words per minute (chars x 12 / seconds, floor) and accuracy x100 in kron + C++ wasm witnessed; per-key error counts and the weakest keys in kron; results in myra.
needs: fs.read, fs.write.
Planned (no runtime yet): Pascal, Mojo, Logo.
