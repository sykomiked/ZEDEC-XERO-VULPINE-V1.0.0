# Wave 29 task: Theorbo (slot 348), Making music together
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 7 (057 fiddlehead), wave 13 (157 luthier), wave 20 (251 swell).
Manifest: `slot 348`, `layer L3`, `version 1.0.0`, `icon icons/l3/348-theorbo.svg`, `budget 233`, model `apps/theorbo_model.{h,c}`, blocks under kernel/src/apps/blocks/theorbo/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Composes, records and performs music with others: scores and stems in the Hoard (`files.read`, `files.write`), rehearsals in a hall (`hall.load`, `hall.play`, `hall.pause`, `hall.seek`, `hall.status`, `pspace.create`), royalty splits paid only on the owners' approval (`ledger.post`). Audio in and out has no method: use only existing methods; list what's missing in your report.
Uses: `use luthier 1` (royalty splits, tuning, level limits, "bar is full"); `use swell 1` (click time per device and latency limits); `use fiddlehead 1` (beat grid and pitch). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `split_ok`: (critical) "royalty split agreed" (every contributor signed the same shares, shares summing to 10000) in Ada as Eve + COBOL as Sutra witnessed
- `mix_level`: (critical) loudness of the final mix (integrated level and true peak in tenths of a dB within the caps) in Ada as Eve + APL as KRON witnessed
- `synth`: audio synthesis for click and guide tones (integer sine table, samples per period) in FORTRAN as Trebago
- `score`: score arrays (bars x parts x events) in APL as KRON
- `versions`: song version tree (take, stem, editor) in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two collaborators keep different takes of bar 9 to 16: the passage is BOTH; the test asserts state 3 and that both takes stay live in the session. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: A royalty payment is a financial action the owners approve.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, mic, peers, net, ledger.read, ledger.post.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending PostScript score`; `pending Simula_I synth`; `pending LabVIEW mix_level`.
Planned (no runtime yet): PostScript, Simula I, LabVIEW.
