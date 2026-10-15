# Wave 5 task: Relief (slot 60), 3D modelling and the depth effect
Read ../APP_WAVE_SPEC.md first.
What it does: models as files in the Hoard (`files.read`, `files.write`).
Logic (critical): vertex transform (rotate, scale, project in fixed point) in C++ wasm + kron witnessed; depth effect layer offsets in kron; mesh volume (signed tetrahedra) exact in trebago; mesh limits (vertices, faces) in eve; model record in myra.
needs: fs.read, fs.write.
Planned (no runtime yet): Julia, Mojo, Simula 67.
