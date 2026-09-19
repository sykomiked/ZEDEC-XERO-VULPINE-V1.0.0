# TOL VOVINA UPAAH LOT — score manifest

Index of every authored `.mid` in `assets/music/`. **13 tracks, 7 957 notes,
76.5 KB, 19 min 54 s of music.**

Vocabulary is the tree's own (`PROVENANCE/TVUL_ROM_FORMAT.md:10-11`): the world
graph is a **hoard**, an enterable place is a **hold** (and each hold IS an
application), an edge is a **gate**. There is no "level" and no "area".

Every file was **independently validated from the raw SMF bytes** — see
§ *Validation* at the foot of this document. All 13 parse clean: no hanging
notes, no chunk-length errors, no missing end-of-track.

---

## The score

| # | file | title | hold | key / mode | tempo | meter | bars | duration | instruments (GM) | how φ / 13 shaped it |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | `hearthscale_hub.mid` | Hearthscale (day) | HEARTHSCALE — hub town, lattice node 0 | D major | 110 | 4/4 | 34 | 1:14 | Brass Section, Clean Guitar, Marimba, Finger Bass, Kit | 34 = F(9) bars split 21+13; tuned-perc cycle of 13 against 4/4 so the hook lands differently on every pass |
| 2 | `hearthscale_ashlight.mid` | Hearthscale, Ashlight | HEARTHSCALE — night / rain variant | D major | 68 | 4/4 | 34 | 2:00 | Flute, Warm Pad, Celesta, Fretless Bass, Kit | same 34-bar φ frame as the day theme, re-scored and halved in tempo; celesta "rain" runs on the 13-cycle |
| 3 | `coilrest_rest.mid` | Coilrest | COILREST — save / rest | D major | 89 | 4/4 | 13 | 0:35 | Glockenspiel, E-Piano, Warm Pad, Acoustic Bass | the whole loop is 13 bars — one bare statement of the system's arity, no drums at all |
| 4 | `bellowsworks_anvil_thirteen.mid` | Anvil Thirteen | THE BELLOWSWORKS — workshop / forge | D dorian → D aeolian | 132 | 4/4 | 34 | 1:02 | Marimba, Brass Section, Tubular Bells, Finger Bass, Kit | 34 = 21+13; anvil ostinato is **13 eighths** (2+2+3+2+2+2), realigning with the barline only every 13 bars; the antecedent returns reharmonised B♮→B♭ |
| 5 | `gildmaw_exchange_open_outcry.mid` | Open Outcry | GILDMAW EXCHANGE — market / trading floor | G mixolydian | 138 | 4/4, swung 16ths (160/80) | 55 | 1:36 | Muted Trumpet, Percussive Organ, Clean Guitar, Acoustic Bass, Kit | 55 = F(10) split 21+13+21; floor bell on a **13-beat** cycle realigning every 52 beats; the two voices swap parts on the reprise |
| 6 | `vellumscale_archive_slow_accretion.mid` | Slow Accretion | VELLUMSCALE ARCHIVE — archive / library | A dorian | 60 | 4/4 | 55 | 3:40 | Celesta, Orchestral Harp, Warm Pad, Choir Aahs, Triangle | 55 = 13+13+13+13+3; the figure **sheds one note per pass** (16→15→14→13), harp canon a fifth below entering at bar 5, fourth pass inverted |
| 7 | `rootwyrm_deep_stonebreath.mid` | Stonebreath | ROOTWYRM DEEP — deep ruins | D phrygian | 72 | 5/4 | 55 | 3:49 | New Age Pad, Marimba, Trombone, Pan Flute, Kit | 55 = 34 atmosphere + 21 forming; **13-beat** ostinato under 5/4 → lcm = 65 beats, realigns only every 13 bars; pad segments 8·5·8·13·8·5·8 = 55, all Fibonacci |
| 8 | `wyrmgate_threshold_sixfold_verdict.mid` | Sixfold Verdict | THE WYRMGATE THRESHOLD — tri-space judgment | D phrygian | 156 | **13/8** (3+3+3+2+2) | 55 | 2:18 | Brass Section, Church Organ, Synth Bass, Timpani, Kit | inverts the relation: the **meter** is 13/8 and the bass cycle is 8 eighths, so lcm(8,13) = 104 eighths = exactly 8 bars; form 5+(8+5+3)+8+21+5 = 55, the statement contracting 8→5→3 as pressure rises |
| 9 | `glintfall_first_gleam.mid` | First Gleam | GLINTFALL — discovery **one-shot** | D major | 120 | 4/4 | 3 | 0:06 | Glockenspiel, Brass Section, Strings, Cymbal | the golden leap inverted — the signature minor 6th turns **major** and climbs; deliberately unlooped |
| 10 | `dragonmark_title.mid` | DRAGONMARK | title / menu face of the hoard (**seed theme**) | D major | 120 | 4/4 | 13 | 0:26 | Trumpet, Brass Section, Church Organ, Finger Bass, Kit | 13-bar title statement over a **13-eighth** bass ostinato; carries the golden-leap seed the rest of the score derives from |
| 11 | `dragonmark_select.mid` | DRAGONMARK (select) | companion-select | D major | 120 | 4/4 | 13 | 0:26 | Marimba, Warm Pad, Finger Bass, Kit | the same 13 bars and the same 13-eighth bass, stripped to four voices — spiral restatement, not a remix |
| 12 | `wingwide_open_sky.mid` | THE WINGWIDE | THE WINGWIDE — open sky / dragon flight | A major | dotted ♩ = 92 (♩ = 138) | 6/8 | 60 | 1:18 | Flute, French Horn, String Ensemble, Contrabass, Kit | 5-bar intro then a 55-bar φ loop; the **major 6th is 17.8 % of all melodic motion** — the golden leap opened out for open air |
| 13 | `chiglets_roost_companion.mid` | CHIGLET'S ROOST | CHIGLET'S ROOST — the AI companion's own space | F major | 104 | mixed: 4/4 with 3/4 at bars 8, 24, 37 and 5/4 at 16, 29 | 37 | 1:25 | Pizzicato Strings, Marimba, Glockenspiel, Acoustic Bass, Kit | the dropped and added beats are Fibonacci-placed rather than square; 4-bar intro then a 33-bar loop that never re-states a bar identically |

---

## The signature interval — "the golden leap"

φ read as a *frequency ratio* is `12 · log₂(φ) = 8.3309` semitones. The nearest
equal-tempered interval is **8 semitones — the ascending minor sixth** — and 8 is
itself F(6). That is the score's signature interval, and it is measurably
present, not decorative:

| file | minor 6ths as share of melodic motion |
|---|---|
| `rootwyrm_deep_stonebreath.mid` | 13.8 % |
| `gildmaw_exchange_open_outcry.mid` | 7.8 % |
| `hearthscale_hub.mid` | 6.7 % |
| `bellowsworks_anvil_thirteen.mid` | 5.1 % |
| `wingwide_open_sky.mid` | 2.4 % m6 — but **17.8 % major 6th**, the leap deliberately opened out |
| `glintfall_first_gleam.mid` | 0 % m6 — the one-shot inverts it to major by design |

---

## Spiral, not circular

The owner's principle — *"recursive and yet never the same way"* — was checked
mechanically by hashing every bar's pitch content and counting distinct bars:

| file | distinct bars |
|---|---|
| bellowsworks, chiglets, coilrest, dragonmark ×2, gildmaw, glintfall, hearthscale ×2 | **100 %** |
| `wingwide_open_sky.mid`, `wyrmgate_threshold_sixfold_verdict.mid` | 98 % |
| `rootwyrm_deep_stonebreath.mid` | 87 % |
| `vellumscale_archive_slow_accretion.mid` | 80 % |

No track contains a single verbatim bar repeat in the majority of its length.
The two lowest scores are the two sparsest ambient pieces, where held pad notes
legitimately recur.

---

## Loop points

Every track except the one-shot carries a **CC#111** loop marker plus a
`LOOP_START` text meta (and `loopStart`/`LOOP_END` aliases) so a sequencer or the
runtime can find the seam.

| file | loop start (ticks) | note |
|---|---|---|
| hearthscale ×2, coilrest, bellowsworks, gildmaw, vellumscale, rootwyrm, dragonmark ×2 | 0 | whole file loops |
| `wyrmgate_threshold_sixfold_verdict.mid` | 15 600 | 5-bar intro plays once, then a 50-bar loop |
| `wingwide_open_sky.mid` | 7 200 | 5-bar intro, then 55 bars |
| `chiglets_roost_companion.mid` | 5 760 | 4-bar intro, then 33 bars |
| `glintfall_first_gleam.mid` | — | **one-shot by design**; the file says so in a text meta |

---

## Tuning

`kernel/src/audiogenomics_pro/audiogenomics_pro.h:72-77` defines
`agp_freq_map_t` with a `bool retune_432` field, and `:137`
`agp_init_freq_map(agp_freq_map_t *fm, bool retune_432)` takes it as an argument
— so **A = 432 Hz is a supported system tuning alongside A = 440**, selectable at
runtime.

These MIDIs are written in **standard pitch**. MIDI note numbers are
tuning-agnostic; which reference pitch note 69 sounds at is a playback decision,
not a property of the file. Nothing here needs to change to audition at either
tuning.

---

## Format

All 13 files are Standard MIDI Files, **format 1, division 480 ticks/quarter**,
General MIDI programme numbers so they audition anywhere. Written by hand in
Python 3, standard library only — no external packages.

Generators (deterministic, byte-identical across runs):

| generator | emits |
|---|---|
| `gen_1_hearthscale.py` | tracks 1–3 |
| `gen_2_working_holds.py` | tracks 4–6 |
| `gen_3_ruins_boss_stinger.py` | tracks 7–9 |
| `gen_4_sky_companion_title.py` | tracks 10–13 |

---

## Originality

Every melody, bassline, chord progression and motif was written for this score.
Nothing is transcribed, quoted, paraphrased or reconstructed from any existing
work. Style only is borrowed, and the stylistic influences appear **exclusively**
as `ref:` comments in the generator headers — never in a filename, a track name,
a title, or any byte inside the shipped `.mid` files, per [[proprietary naming]].

That was checked mechanically: the only text metas in the 13 files are track
names, loop markers, and the composers' own structural annotations. **No
franchise name appears in any shipped byte.**

Each hold has exactly one seed motif, stated once and thereafter only
transformed — inverted, reharmonised, displaced, reorchestrated, transposed.
Derived material cannot be a quotation of anything external; that is a
structural defence rather than a promise.

---

## Validation

Independently re-verified by parsing the raw bytes — not by trusting the
generators. Per file: `MThd` well-formed and `ntrks` matching the actual `MTrk`
count; every chunk's declared length matching its real length; every delta-time
varint decoding inside its chunk; `FF 2F 00` ending every track; every note-on
matched by a note-off; tempo and time-signature metas present; percussion on
channel 10.

**Result: 13 / 13 PASS.** 0 hanging notes, 0 overlapping duplicate note-ons,
0 zero-length notes, 0 notes sounding past end-of-track, 0 chunk-length
mismatches across all 78 303 bytes.

Two cosmetic observations, neither a defect:

- `vellumscale_archive_slow_accretion.mid` leaves ~2 beats (2.0 s at ♩=60) of
  silence before the loop seam. This is the deliberate "void" the composer
  documents — it is what makes the loop click-free — but it is audible as a
  breath in an otherwise continuous archive ambience.
- `rootwyrm_deep_stonebreath.mid` track 3 *Horizon Brass* carries only 4 notes
  across 3:49. Intentional for a sparse deep-ruins horizon line, but it is the
  thinnest track in the score and worth a listen before ship.
