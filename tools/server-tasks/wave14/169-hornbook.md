# Wave 14 task: Hornbook (slot 169), Lessons and progress
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (041 footlight), wave 8 (098 sapling, 106 nutshell, 107 gnomon, 099 typewright).
Manifest: `slot 169`, `layer L2`, `icon icons/l2/169-hornbook.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: builds lessons with diagrams, slides and flashcards in the Hoard (`files.read`, `files.write`), then tracks each learner's progress.
Uses: `use footlight 1` (slide scale, rehearsal timing, run order); `use sapling 1` ("lesson unlocked", progress, results); `use nutshell 1` (next box and due day); `use gnomon 1` (area and side lengths for diagrams); `use typewright 1` (words per minute, accuracy). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "age-appropriate" (the lesson's band at most the learner's) in Ada as Eve + APL as KRON witnessed; next lesson step from the learner's answer in BASIC (twin Hatchling) + APL as KRON witnessed; enrolment records (learner, class, seat; seats per class refused on overflow) in COBOL as Sutra; score statistics (mean floor, min, max per question) exact in FORTRAN as Trebago; mastery grid (learner by skill, mastered after k right in a row) in APL as KRON; learner tree in MUMPS as MYRA.
BOTH test: question 7 lists 1/2 and 2/4 as valid; a learner answers 2/4 and one answer key says right, the other wrong: the answer is BOTH and is accepted, not marked wrong.
Twin form: the BASIC block is committed in its twin form (Hatchling) and its manifest logic line names `hatchling`.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write.
Planned (no runtime yet): Mojo, Bash.
