# Wave 8 task: Typewright (slot 99), Typing tutor
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 99, build position 96 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: typing lessons and drills; lessons and results in the Banker (`files.read`, `files.write`).
Logic (critical): words per minute (chars x 12 / seconds, floor) and accuracy x100 in kron + C++ wasm witnessed; per-key error counts and the weakest keys in kron; results in myra.
needs: fs.read, fs.write.
Now runs (Lingua front-end built): Pascal (twin Tutordrake) for step-by-step teaching: `logic pascal <entry> apps/blocks/typewright/<entry>.pas`, in the twin's canonical form, joined to another language's line for the same entry; Logo (twin Kiteling) for geometry: `logic logo <entry> apps/blocks/typewright/<entry>.lg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): Mojo.
