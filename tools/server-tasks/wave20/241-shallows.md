# Wave 20 task: Shallows (slot 241), Live conversation translation
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 2 (028 halloo); wave 4 (046 wordhoard); wave 5 (068 parley); wave 11 (135 ticker).
Manifest: `slot 241`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/241-shallows.svg`, `budget 233`, model `apps/shallows_model.{h,c}`, blocks under kernel/src/apps/blocks/shallows/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Translates a live conversation between two people with captions on both sides: language and translation (`xlate.detect`, `xlate.languages`, `xlate.translate`), the call (`call.start`, `call.status`, `call.signal`), the glossary in the Hoard (`files.read`). Speech recognition and speech output have no method (as Parley): use only existing methods; list what's missing in your report.
Uses: `use parley 1` ("text sent" check and detection limits); `use ticker 1` ("caption stays long enough"); `use halloo 1` ("may this call ring now" and call duration); `use wordhoard 1` (headword lookup and "did you mean"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) latency limits (a caption later than 2000 ms end to end is marked late, never shown as live) in Ada as Eve + APL as KRON witnessed
- (critical) "show one meaning" only when the best sense's confidence x100 clears the threshold and the next is at least 20 points below it, in FORTRAN as Trebago + APL as KRON witnessed
- phrase matrix (phrases x senses, alignment counts) in APL as KRON
- glossary records and the language tree in MUMPS as MYRA
- Sutra is left out: no money or counted business records; the glossary is MYRA's
Paraconsistent (BOTH test, in test_zxapp.c): "bank" with two senses both above the threshold and within 20 points gives BOTH; the test asserts state 3 and that both meanings are shown to the speakers to clarify. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Translation is advisory: clinical, legal or safety talk needs a human check.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: mic, peers, ai.ask, fs.read, notify.
Planned (no runtime yet): Mojo, Raku.
