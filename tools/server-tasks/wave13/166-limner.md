# Wave 13 task: Limner (slot 166), Painting
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (053 gesso), wave 6 (059 bellows), wave 10 (116 dyer).
Manifest: `slot 166`, `layer L2`, `icon icons/l2/166-limner.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: paints and edits images in the Hoard (`files.read`, `files.write`) with palettes and optional AI help (`swarm.ask`, `swarm.state`) held for approval.
Uses: `use gesso 1` (pixel operations, histogram, brush and canvas limits, layer record); `use bellows 1` ("within budget", token cost, request record); `use dyer 1` (RGB to HSL, contrast). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): nearest palette colour (smallest squared distance over the palette vectors) in APL as KRON + FORTRAN as Trebago witnessed; "may this AI stroke be applied" (accepted by the person AND within budget) in Ada as Eve + APL as KRON witnessed; asset licences (asset, licence code, attribution) as fixed records in COBOL as Sutra; canvas limits in Ada as Eve; pixel arrays in APL as KRON; layer tree in MUMPS as MYRA.
BOTH test: collaborators A and B both paint layer 3 over the same region: the layer holds both stroke sets as BOTH alternatives until one is chosen.
Never imitate a real person's face without their recorded consent (Bellows' rule).
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, ai.ask, swarm, notify.
Planned (no runtime yet): MATLAB, Mojo.
