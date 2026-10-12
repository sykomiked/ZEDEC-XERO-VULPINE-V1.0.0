# Built-in apps and toolbox

ZXV ships its own everyday apps, with names of its own: a file manager, a
text editor, a photo and media viewer and more come built in. Each one is a
view in the ZXV window (`kernel/arch/hosted/zxv_ui.html`), which the Mac app shows in its own window and Linux and Windows open in the
browser, so one build gives all three the same apps. Each view does its work
through zxvd's JSON-RPC methods ([ZXVD.md](ZXVD.md),
[api/zxvd.openrpc.json](../api/zxvd.openrpc.json)), so ZXV Studio, the phone
companion and the SDKs can use the same calls.

The window reaches zxvd through `POST /api/rpc`: one JSON-RPC request in, its
response out, the same as a line on the control socket, and behind the same
guard as the rest of the window's API (`zxv_http_guard.h`: launch token, host,
origin and fetch-metadata checks).

## What is in this release

| Who | App | What it does | zxvd methods |
|---|---|---|---|
| Everyone | **Burrow** (files) | Browse the ZXV folder, make folders and text files, import files (button or drag and drop), rename, move to the Trash, put back, empty the Trash. | `files.*` |
| Everyone | **Quillon** (text) | Plain text and Markdown, save and save as (⌘S / Ctrl+S), a live Markdown preview, a prose or code font, word, character and line counts. Unsaved work is kept as a draft in the window and restored after a reload. | `files.read`, `files.write` |
| Everyone | **Glint** (photos and media) | Thumbnails of the images in a folder, a full-size viewer with arrow keys, and playback for audio and video files. | `files.list`, `files.read` |
| Everyone | **Murmuration** (the swarm) | The live swarm: agents, budget, cycles, ledger, network, updates, notifications and the companion chat (the earlier single-page window). | `swarm.*`, `net.*`, … |
| Developers | **Spindle** (console) | Pick any zxvd method, give parameters as JSON, see the reply. | every method (`rpc.discover`) |

The views above are interim. The native views come from ZXV's own toolkit
(`kernel/src/zui`); each app's logic already lives apart from any view, as a
model the toolkit draws.

## App models

Each app is a toolkit-neutral C model in `kernel/src/apps/`: a state struct
plus action functions that call zxvd, with no drawing and no allocation. A
view (`kernel/src/zui/views/<app>.c`, or any other front end) reads the state
and calls the actions. The shared rules are in `zxm.h` (M1-M5):

- **One link to zxvd** (`zxm_link_t`): any transport that sends one JSON-RPC
  line and returns one, so the same model runs over the control socket, the
  window's `/api/rpc`, or zxvd's dispatcher in the same process.
- **AI is held.** Swarm output enters the model as pending text
  (`zxm_held_t`); nothing changes until the owner accepts it, and rejecting
  it drops it. An edit made while an answer is pending drops the answer.
- **AI is costed.** Every AI action records the agent that ran it and its
  token count. zxvd's `swarm.ask` does not report a count yet, so the count is
  an estimate and says so (`exact = false`).
- **A palette table per app** (`BURROW_AI`, `QUILLON_AI`, `GLINT_AI`,
  `SPINDLE_AI`): name, agent and estimated tokens, for the command palette.

| Model | State | Actions | AI |
|---|---|---|---|
| `burrow_model.h` | folder, entries, selection | open, up, enter, mkdir, new text, rename, trash, put back, empty Trash, import | describe the folder; suggest folders (names only, never moves files) |
| `quillon_model.h` | one UTF-8 document, path, unsaved flag, counts | new, open, save, save as, replace, set text | summarize, rewrite, continue, proofread |
| `glint_model.h` | the media in a Burrow folder, the one viewed | view, next, prev, load bytes into the view's buffer | name the album (file names only) |
| `spindle_model.h` | method list, the last 16 calls | discover, call with JSON params | explain a reply |

Burrow and Quillon carry the space they work in (`burrow_set_space`, and the
`space` argument of `quillon_open` and `quillon_save_as`); Glint follows its
Burrow folder. `burrow_cross` copies an entry into another space and reports
a held crossing's ticket for `burrow_cross_confirm`.

`kernel/src/apps/test_apps.sh` runs the models against zxvd's real dispatcher
on a scratch ZXV folder.

## The ZXV folder

Every app works inside one folder, never outside it (`kernel/arch/hosted/zxv_files.h`):

- **Where:** `~/Documents/ZXV` on macOS and Linux (`~/ZXV` when there is no
  `~/Documents`), `%USERPROFILE%\Documents\ZXV` on Windows, or the
  `ZXV_FILES_ROOT` environment variable. It is an ordinary folder, so the
  host system's own file manager sees the same files.
- **Names:** no `..`, no absolute paths, no hidden names, no `\` or `:` and no
  control characters; a symbolic link anywhere inside refuses the path, so
  nothing can lead outside the folder.
- **Saving is whole:** a save goes to a staging file that is flushed and then
  renamed over the old file, so a crash leaves the old file or the new one,
  never half of each. Long files are sent in parts and appear only when the
  last part arrives.
- **Trash, not delete:** deleting moves an item into `.Trash` in the folder;
  only "Delete now" in the Trash or "Empty Trash" removes it for good.

## Spaces

ZXV is laid out like the real world: your home, offices, team rooms, your
cloud top (your public front, which does not have to be online) and parks
(commons). Each space has its own folder with its own Trash, and a manifest
naming its kind, owner and members (`kernel/arch/hosted/zxv_spaces.h`). Every
`files.*` call takes an optional `space`, and zxvd checks the caller's rights
on each call, so a hidden button in the interface is never the only guard.

| Kind | Owner | Member | Guest | Anyone else |
|---|---|---|---|---|
| Home | view, change | | | |
| Office | view, change | view, change | view | |
| Team | view, change | view, change | view | |
| Cloud top | view, change | view | view | view |
| Park | view, change | view, change | view, change | view, change |

- **Crossings are held.** `files.cross` copies a file into another space.
  When that would show it to anyone who cannot see it now (a public space, or
  a member who is not in the source), zxvd holds it and returns a ticket; the
  copy happens only when the owner confirms with that ticket.
- **Joined spaces.** `spaces.join` records a space another node invited this
  one into, with the role it was given. Until the mesh syncs spaces, the check
  guards this node's copy, and the owner's node checks again when changes
  reach it.
- **Not yet:** syncing spaces between nodes, publishing a cloud top or park
  under a CID, and group decisions on members (the native interface's
  governance model, `zui_space.c`, decides; zxvd records the result).

## Names

Every app has a ZXV name, not another company's product name. A trademark
search is still needed before release; the names below were chosen to avoid
the obvious clashes. Planned names for apps not built yet: Margins (notes),
Coffer (wallet and books), Waymark (Web4 viewer), Commons (chat), Tidewatch
(activity monitor), Keystone (settings), Witness (audit log), Lodestone
(inspector), Satchel (packager).

## Coming next

In the order they will be built (the plan in the "Built-in apps and toolbox" thread):

| Who | App | Built on |
|---|---|---|
| Everyone | Music and video player with playlists | Glint's playback |
| Everyone | Notes | `kernel/src/apps/notes_app.c` |
| Everyone | Wallet and books | the `ledger.*` methods (ZXV Studio already has a Ledger view) |
| Everyone | Web4 viewer | `kernel/src/browser`, `kernel/src/web4` |
| Everyone | Chat | Commons chat (PR #78) |
| Everyone | Image and voice generation | the Images and voice work (PR #93) |
| Power users | Activity monitor, Settings, Audit log | `swarm.state`, `net.*`, `update.*`, `notes.list`, the quarantine log |
| Developers | Inspector, Packager | `rpc.discover`, `model.info`, `net.peers`; AIPI for signed bundles |
| Developers | Playground | owned by the language examples and templates work |
