# Wave 4 task: Gesso (slot 53), Image editing and painting
Read ../APP_WAVE_SPEC.md first.
What it does: images from the Hoard (`files.read`, base64); an edit is saved as a new file (`files.write`), never over the original until the owner says so.
Logic (critical): pixel operations over whole rows (brightness, contrast, blend, clamped 0..255) in kron + C++ wasm witnessed; histogram counts in kron + trebago; brush and canvas limits in eve; layer record in myra.
needs: fs.read, fs.write.
Planned (no runtime yet): Julia, LabVIEW.
