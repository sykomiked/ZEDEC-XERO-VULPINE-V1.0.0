# Wave 9 task: Gnomon (slot 107), Geometry and maths explorer
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 107, build position 104 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: exact shapes and numbers; saves in the Banker (`files.read`, `files.write`).
Logic (critical): twice a polygon's area (shoelace, exact) and squared side lengths in kron + C++ wasm witnessed; fractions reduced with `use number_classics 1`; saved figures in myra. Turtle paths stay in C (Logo has no runtime); say so in the manifest.
needs: fs.read, fs.write.
Now runs (Lingua front-end built): Logo (twin Kiteling) for geometry: `logic logo <entry> apps/blocks/gnomon/<entry>.lg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): F#, Python.
