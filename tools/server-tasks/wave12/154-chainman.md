# Wave 12 task: Chainman (slot 154), Land survey
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (085 waymark), wave 6 (040 furrow), wave 7 (054 burin), wave 8 (093 understory).
Manifest: `slot 154`, `layer L2`, `icon icons/l2/154-chainman.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: surveys land parcels, boundaries and waypoints (`place.here`, `place.add`, `place.list`) into maps with measured records in the Hoard (`files.read`, `files.write`). zxvd has no method for a general signing method for a survey record (no daemon method signs an arbitrary file): use only existing methods; list what's missing in your report.
Uses: `use furrow 1` (column sums of the field book); `use burin 1` (curve flattening for curved boundaries, path record); `use waymark 1` (`waymark.place_ok` on every corner); `use understory 1` (land entries). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): parcel area (twice the area by the shoelace rule over corner vectors in millimetres, exact) in FORTRAN as Trebago + APL as KRON witnessed; "closes within tolerance" (closure error at most the declared millimetres per 100 m) in Ada as Eve + APL as KRON witnessed; title register (title number, parcel, area) as fixed records refused on overflow in COBOL as Sutra; parcel grid (cells claimed by each parcel, overlaps found) in APL as KRON; land hierarchy in MUMPS as MYRA.
BOTH test: parcels 3 and 4 both claim grid cell (12, 7): the cell is BOTH, shown as disputed, and neither parcel's record is overwritten.
Survey records are advisory; legal title is a person's and the land registry's decision.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, fs.read, fs.write.
Now runs (Lingua front-end built): Logo (twin Kiteling) for geometry: `logic logo <entry> apps/blocks/chainman/<entry>.lg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): Haskell, ML.
