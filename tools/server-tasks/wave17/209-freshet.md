# Wave 17 task: Freshet (slot 209), New app project
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (007 satchel), wave 10 (121 joinery, 124 graft, 125 whetstone).
Manifest: `slot 209`, `layer L2`, `icon icons/l2/209-freshet.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: hatches a new app project from a template in the Hoard (`files.mkdir`, `files.write`, `files.read`, `files.list`), with signing, version control and a first build ready. zxvd has no method for build run and signing (as Whetstone and Satchel report): use only existing methods; list what's missing in your report.
Uses: `use satchel 1` (package limits, package record); `use joinery 1` (buffer limits); `use graft 1` (history intact, commit record); `use whetstone 1` (overall verdict, counts). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "within the template's declared limits" (budget, needs only names `zxapp_cap_known` accepts, layer matching the slot) in Ada as Eve + APL as KRON witnessed; template matrix (template by setting) in APL as KRON; licence records (project, licence code) as fixed records in COBOL as Sutra; build timings exact in FORTRAN as Trebago; project tree in MUMPS as MYRA.
BOTH test: template A sets budget 233 and template B 89 for the same project: the setting is BOTH and the author chooses; neither overwrites the other.
Never copy anything from build_system/keys/.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, sys.read, notify.
Planned (no runtime yet): Bourne shell, REXX, PostScript.
