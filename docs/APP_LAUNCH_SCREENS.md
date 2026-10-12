# App launch screens

Every ZXV app opens with the boot screen itself, played inside the window the person opened for that app. The big
top-centre logo is the app's icon instead of the OS fox, the app's load parts sit on the tiles where the boot has its
five houses, and the vault opens inside the window onto the app. Each app keeps its own beginning, middle and end
sound and a faint motion of its own behind the tiles. One screen, a theme per app, so 144 apps do not mean 144
bespoke screens.

Status: six apps mocked up for the owner's approval (Burrow, Flint, Murmuration, Coffer, Cutwater, Commons).
The logic (`kernel/src/launch/zxl_launch.{h,c}`) is built and tested; drawing plugs into zui's
`zui_launch_draw` (Native GUI work) once that lands.

## The sequence

Same grammar as the boot, so the whole system speaks one loading language:

| Step | What you see | What you hear | What drives it |
|---|---|---|---|
| Begin | The app's window opens on the boot screen: its icon dim at the top, its load parts on carved tiles, its ambient motion behind | The begin cue | The launch starting |
| Gather | Two mirrored gold bars run inward; each part lights as the head reaches it | One pitched tick per part, climbing the app's scale; the centre part plays the top note | Each part's **real** load finishing |
| Light | The icon lights and warms, one sweep of light | Nothing | Every part loaded |
| Release | The bar under the icon grows centre-out while the vault's bolts lock across the seam | Nothing | The rest of the real load; unknown time breathes, never a fake number |
| Open | The bolts throw back and the window's face splits down the middle onto the app | The end cue | The app reporting ready, never before |

The whole sequence happens inside the app's window, not full screen: the toolkit opens the window, the launch
screen fills its content area, and the vault opens onto the app in that window. Switching between open windows
does not replay it.

A warm launch (the app is already loaded) skips straight to the vault opening and the end cue. Nothing is ever padded
to make the screen last longer than the load.

Downloads, copies and installs keep a plain green and gold bar, as before.

## What a theme carries

| Field | Meaning |
|---|---|
| Key | The app's key. The OS boot keeps E (chime, coins, gong); group defaults never use E so the boot stays the biggest moment. Murmuration, the swarm itself, is the one app in E. |
| Timbre | glass, marimba, chip, flute, bell, coin, pluck, strings |
| Icon | The owner's drawing (`zxv-brand/app-icons/NNN-name.png`), in the fox's place; a stand-in mark until it arrives |
| Ambient | arches, sparks, flock, dust, water, rings: faint motion behind the tiles |
| Parts | The app's own load steps, from its manifest (Burrow: Index, Spaces, Places, Trash, Sync) |

Twelve group defaults cover all 144 apps; an app gets its own entry when it earns one.

| Group | Key | Timbre | Ambient |
|---|---|---|---|
| 1 System core | D | marimba | arches |
| 2 Terminal, console, monitors | A | chip | sparks |
| 3 Network, mesh, Web4 | F# | pluck | rings |
| 4 Writing and office | Bb | glass | dust |
| 5 Media and creative | C | strings | water |
| 6 AI and the swarm | B | flute | flock |
| 7 Money and the economy | G | coin | dust |
| 8 Places, spaces, life | F | pluck | rings |
| 9 Learning, play, family | Eb | marimba | sparks |
| 10 Desk utilities | Ab | glass | dust |
| 11 Developer tools | Db | chip | sparks |
| 12 Access, language, safety | F | glass | rings |

The six apps with their own themes:

| App | Begin | Middle (per part) | End | Behind the tiles |
|---|---|---|---|---|
| Burrow (Files) | A knock on wood, marimba D A D | Marimba up D pentatonic | The earth giving way, a warm D chord | Tunnel arches breathing |
| Flint (Terminal) | Flint struck on steel, a spark ping | Keystrokes that play notes | A tube warming, a thunk, an A chord | Sparks rising |
| Murmuration (Swarm) | A flutter of wings, breathy flute | A chirp per agent waking | The flock lifting off, an E chord | A flock wheeling |
| Coffer (Wallet) | A key turning the tumblers, a bell | A coin per part | The latch, bells, coins settling | Gold dust drifting |
| Cutwater (Video editing) | The projector reel spinning up, low strings | A frame clicking through the gate | The clapper, a low hit, a C string swell | Water lines flowing |
| Commons (Chat) | A call and its answer, left and right | A kalimba pluck per part | A bubble popping open, an F chord | Rings spreading like voices |

## Sound setting

The owner's earlier rule was "sound only on the OS boot; app launches are silent". The 2026-10-12 ask changes
that for app launches. Launch sounds are **on by default**, with a setting to turn them off:

- Keystone > Sound > **App launch sounds** (setting key `sound.app_launch`, default on), plus a launch volume.
- Silent whenever the system is muted or a focus mode (Stillwater) is on.
- With a screen reader (Nightjar) speaking, the ticks are dropped and begin/end play ducked to 96/256, so a
  launch never talks over speech.
- Reduce motion swaps the vault for a short cross-fade.

## Files

- `kernel/src/launch/zxl_launch.{h,c}`: themes, tick notes, sound policy, and the event sequence driven by real
  progress. Integer only, freestanding. Test: `kernel/src/launch/test_zxl_launch.c` (in `make verify-all`).
- `tools/launch/launch_sounds.py`: synthesises each app's three cues from scratch (no samples), deterministic.
- `tools/launch/launch_mock.py`: renders the mockup videos with their sounds placed where the events happen.

```sh
python3 tools/launch/launch_sounds.py out/sounds          # begin / middle / end WAVs per app
python3 tools/launch/launch_mock.py out [burrow flint ...] # MP4 with sound + stills (needs ffmpeg)
# ZXV_APP_ICONS=<dir of NNN-name.png>  ZXV_DESKTOP=<desktop png behind the window>  ZXV_FONT=<ttf>
```

## Next

1. Owner approves or adjusts the six.
2. Wire `zxl_launch` to `zui_launch_draw` / `zui_vault_draw` drawn into the app's window and to the audio mixer;
   apps declare their parts and theme in the manifest, and the toolkit's window-open hook starts the launch
   (Native app toolkit).
3. Generate the rest from the group defaults, then give more apps their own entries as their icons arrive.
