# Wave 26 task: Lunette (slot 319), Life ceremonies
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 6 (044 kindred), wave 8 (095 bonfire), wave 13 (164 handfast), wave 15 (182 mummer), wave 20 (242 silt).
Manifest: `slot 319`, `layer L3`, `icon icons/l3/319-lunette.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: records life ceremonies such as weddings, namings and funerals: bookings, officiants, certificates and guests (`files.read`, `files.write`, `social.groups`).
Uses: `use handfast 1` ("ready to sign"); `use silt 1` (records office); `use mummer 1` (cue start times for the service); `use bonfire 1` ("may this guest join", seats left); `use kindred 1` (contact index). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `notice_ok` (the declared legal notice period between filing and the ceremony day is met) in Ada as Eve + APL as KRON witnessed; `guest_capacity` (guests at most the venue's seats, with the RSVP counts) in FORTRAN as Trebago + APL as KRON witnessed; `fees` (ceremony fees in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `venues` calendar grid in APL as KRON; `family` tree in MUMPS as MYRA.
BOTH test: the civil record names the bride Ana Ruiz and the religious record Ana Ruiz Soto: both are kept as BOTH, each valid in its own registry. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Legal records are the registrar's; the app records.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, peers, net, notify.
Planned (no runtime yet): RPG, Perl, TeX.
Pending: `pending RPG fees` (report); Perl (text): no entry here is an obvious fit; put `pending Perl <entry>` on the one closest to text and name it in your report; TeX (typesetting): no entry here is an obvious fit; put `pending TeX <entry>` on the one closest to typesetting and name it in your report.
