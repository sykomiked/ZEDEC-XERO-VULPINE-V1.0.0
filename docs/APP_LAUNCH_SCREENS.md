# App launch screens

Every ZXV app opens with a launch screen made with the boot screen's care, themed to the app, with its own
beginning, middle and end sound. One sequence, a theme per app, so 144 apps do not mean 144 bespoke screens.

Status: six apps mocked up for the owner's approval (Burrow, Flint, Murmuration, Coffer, Cutwater, Commons).
The logic (`kernel/src/launch/zxl_launch.{h,c}`) is built and tested; drawing plugs into zui's
`zui_launch_draw` (Native GUI work) once that lands.

## The sequence

Same grammar as the boot, so the whole system speaks one loading language:

| Step | What you see | What you hear | What drives it |
|---|---|---|---|
| Begin | The app's mark dim at the top, its load parts on carved tiles, its ambient motion behind | The begin cue | The launch starting |
| Gather | Two mirrored gold bars run inward; each part lights as the head reaches it | One pitched tick per part, climbing the app's scale; the centre part plays the top note | Each part's **real** load finishing |
| Light | The mark lights and warms, one sweep of light | Nothing | Every part loaded |
| Release | The bar under the mark grows centre-out | Nothing | The rest of the real load; unknown time breathes, never a fake number |
| Reveal | The screen opens onto the app in the app's own way | The end cue | The app reporting ready, never before |

A warm launch (the app is already loaded) skips straight to the reveal and the end cue. Nothing is ever padded
to make the screen last longer than the load.

Downloads, copies and installs keep a plain green and gold bar, as before.

## What a theme carries

| Field | Meaning |
|---|---|
| Key | The app's key. The OS boot keeps E (chime, coins, gong); group defaults never use E so the boot stays the biggest moment. Murmuration, the swarm itself, is the one app in E. |
| Timbre | glass, marimba, chip, flute, bell, coin, pluck, strings |
| Reveal | iris (tunnel), scanline (CRT), scatter (flock), lid (strongbox), wake (water off a bow), petals (four rooms). The vault split stays the boot's own. |
| Ambient | arches, sparks, flock, dust, water, rings: faint motion behind the tiles |
| Parts | The app's own load steps, from its manifest (Burrow: Index, Spaces, Places, Trash, Sync) |

Twelve group defaults cover all 144 apps; an app gets its own entry when it earns one.

| Group | Key | Timbre | Reveal | Ambient |
|---|---|---|---|---|
| 1 System core | D | marimba | iris | arches |
| 2 Terminal, console, monitors | A | chip | scanline | sparks |
| 3 Network, mesh, Web4 | F# | pluck | petals | rings |
| 4 Writing and office | Bb | glass | petals | dust |
| 5 Media and creative | C | strings | wake | water |
| 6 AI and the swarm | B | flute | scatter | flock |
| 7 Money and the economy | G | coin | lid | dust |
| 8 Places, spaces, life | F | pluck | iris | rings |
| 9 Learning, play, family | Eb | marimba | petals | sparks |
| 10 Desk utilities | Ab | glass | scanline | dust |
| 11 Developer tools | Db | chip | scanline | sparks |
| 12 Access, language, safety | F | glass | iris | rings |

The six apps with their own themes:

| App | Begin | Middle (per part) | End | Reveal |
|---|---|---|---|---|
| Burrow (Files) | A knock on wood, marimba D A D | Marimba up D pentatonic | The earth giving way, a warm D chord | Iris: a tunnel opens |
| Flint (Terminal) | Flint struck on steel, a spark ping | Keystrokes that play notes | A tube warming, a thunk, an A chord | Scanline: folds to a line, opens into the app |
| Murmuration (Swarm) | A flutter of wings, breathy flute | A chirp per agent waking | The flock lifting off, an E chord | Scatter: the gold flies off as a flock |
| Coffer (Wallet) | A key turning the tumblers, a bell | A coin per part | The latch, bells, coins settling | Lid: the box lifts open |
| Cutwater (Video editing) | The projector reel spinning up, low strings | A frame clicking through the gate | The clapper, a low hit, a C string swell | Wake: the screen parts like water off a bow |
| Commons (Chat) | A call and its answer, left and right | A kalimba pluck per part | A bubble popping open, an F chord | Petals: four rooms open |

## Sound setting

The owner's earlier rule was "sound only on the OS boot; app launches are silent". The 2026-10-12 ask changes
that for app launches. Launch sounds are **on by default**, with a setting to turn them off:

- Keystone > Sound > **App launch sounds** (setting key `sound.app_launch`, default on), plus a launch volume.
- Silent whenever the system is muted or a focus mode (Stillwater) is on.
- With a screen reader (Nightjar) speaking, the ticks are dropped and begin/end play ducked to 96/256, so a
  launch never talks over speech.
- Reduce motion swaps every reveal for a short cross-fade.

## Files

- `kernel/src/launch/zxl_launch.{h,c}`: themes, tick notes, sound policy, and the event sequence driven by real
  progress. Integer only, freestanding. Test: `kernel/src/launch/test_zxl_launch.c` (in `make verify-all`).
- `tools/launch/launch_sounds.py`: synthesises each app's three cues from scratch (no samples), deterministic.
- `tools/launch/launch_mock.py`: renders the mockup videos with their sounds placed where the events happen.

```sh
python3 tools/launch/launch_sounds.py out/sounds          # begin / middle / end WAVs per app
python3 tools/launch/launch_mock.py out [burrow flint ...] # MP4 with sound + stills (needs ffmpeg)
```

## Next

1. Owner approves or adjusts the six.
2. Wire `zxl_launch` to `zui_launch_draw` and the audio mixer; apps declare their parts and theme in the manifest
   (Native app toolkit).
3. Generate the rest from the group defaults, then give more apps their own entries as their icons arrive.
