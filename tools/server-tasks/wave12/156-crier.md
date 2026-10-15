# Wave 12 task: Crier (slot 156), One announcement, every channel
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 2 (027 commons), wave 5 (035 aerie, 036 gazette), wave 11 (135 ticker).
Manifest: `slot 156`, `layer L2`, `icon icons/l2/156-crier.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: broadcasts one announcement to feeds (`social.post`), chat rooms (`chat.post`, `pspace.post`), radio (`harmonic.encode`, `ehop.bands`) and captions at once.
Uses: `use commons 1` ("may I post here", text limits); `use aerie 1` (frame tag split, "fits the band", message log); `use gazette 1` ("show this item", unread counts); `use ticker 1` (caption stays long enough). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "may broadcast to this channel" = the sender may post there AND within the channel's rate limit AND the text fits it (radio frame bytes, caption length) in Ada as Eve + APL as KRON witnessed; rate limits (at most n a channel an hour) in Ada as Eve; notice register (notice number, sends per channel) refused on overflow in COBOL as Sutra; reach (members known per channel, summed) exact in FORTRAN as Trebago; channel matrix (notice by channel: sent, held) in APL as KRON; audience tree in MUMPS as MYRA.
BOTH test: seats 2 and 5, both authorities for topic 9, issue notices with different text: both are shown together as BOTH; neither replaces the other.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, net, notify.
Planned (no runtime yet): Gleam, Mojo, Icon.
