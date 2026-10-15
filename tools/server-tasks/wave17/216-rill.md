# Wave 17 task: Rill (slot 216), Children's reading
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 2 (139 lamplight), wave 7 (046 wordhoard), wave 9 (103 cubby), wave 10 (108 talewind).
Manifest: `slot 216`, `layer L2`, `icon icons/l2/216-rill.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: gives children a safe reading space with read-aloud, word help and story choices, from the Hoard (`files.read`) within their screen time (`screen.check`, `screen.status`). zxvd has no method for text-to-speech: use only existing methods; list what's missing in your report.
Uses: `use cubby 1` ("may this child open this now"); `use lamplight 1` (layout limits, minutes to read); `use wordhoard 1` (headword lookup, "did you mean"); `use talewind 1` ("choice shown"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "age-appropriate" (the book's band at most the child's) in Ada as Eve + APL as KRON witnessed; reading level (words per sentence and syllables per word, integer formula) in FORTRAN as Trebago + APL as KRON witnessed; word help steps (sound it out, meaning, example) in BASIC (twin Hatchling); vocabulary grid (child by word) in APL as KRON; child profile tree in MUMPS as MYRA.
BOTH test: the teacher sets level 3 and the app computes level 5: both are shown as BOTH; neither judgement silently wins.
Twin form: the BASIC block is committed in its twin form (Hatchling) and its manifest logic line names `hatchling`.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, screen.time, sys.read.
Planned (no runtime yet): Mojo, AWK.
