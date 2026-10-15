# Wave 8 task: Sapling (slot 98), Courses and lessons
Read ../APP_WAVE_SPEC.md first.
What it does: courses kept in the Banker (`files.read`, `files.write`); coding lessons run through `play.catalogue` and `play.run`.
Logic (critical): "lesson unlocked" (every prerequisite passed) in kron + basic witnessed; progress x100 and quiz scores in kron; courses and results in myra.
needs: fs.read, fs.write.
Planned (no runtime yet): Python, Logo.
