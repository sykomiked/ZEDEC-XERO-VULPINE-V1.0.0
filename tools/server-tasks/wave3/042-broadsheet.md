# Wave 3 task: Broadsheet (slot 42), Document viewer and annotation
Read ../APP_WAVE_SPEC.md first.
What it does: open documents from the Hoard (`files.read`, base64), print (`print.submit`); annotations go to a separate file beside the original (`files.write`), never into it.
Logic (critical): "well formed" (header present, every cross-reference offset inside the file size) in Rust wasm + eve witnessed, so a malformed file is refused; zoom to fit page in kron; annotation record in myra.
needs: fs.read, fs.write, print.
Planned (no runtime yet): Perl, PostScript, TeX.
