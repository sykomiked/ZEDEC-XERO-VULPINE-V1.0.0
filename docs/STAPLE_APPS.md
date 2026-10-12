# Built-in apps and toolbox

ZXV ships its own everyday apps, the way a Mac comes with Finder, TextEdit and
Photos. Each one is a view in the ZXV window (`kernel/arch/hosted/zxv_ui.html`),
which the Mac app shows in its own window and Linux and Windows open in the
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
| Everyone | **Files** | Browse the ZXV folder, make folders and text files, import files (button or drag and drop), rename, move to the Trash, put back, empty the Trash. | `files.*` |
| Everyone | **TextEdit** | Plain text and Markdown, save and save as (⌘S / Ctrl+S), a live Markdown preview, a prose or code font, word, character and line counts. Unsaved work is kept as a draft in the window and restored after a reload. | `files.read`, `files.write` |
| Everyone | **Photos** | Thumbnails of the images in a folder, a full-size viewer with arrow keys, and playback for audio and video files. | `files.list`, `files.read` |
| Everyone | **Swarm** | The live swarm: agents, budget, cycles, ledger, network, updates, notifications and the companion chat (the earlier single-page window). | `swarm.*`, `net.*`, … |
| Developers | **Console** | Pick any zxvd method, give parameters as JSON, see the reply. | every method (`rpc.discover`) |

## The ZXV folder

Every app works inside one folder, never outside it (`kernel/arch/hosted/zxv_files.h`):

- **Where:** `~/Documents/ZXV` on macOS and Linux (`~/ZXV` when there is no
  `~/Documents`), `%USERPROFILE%\Documents\ZXV` on Windows, or the
  `ZXV_FILES_ROOT` environment variable. It is an ordinary folder, so Finder,
  Explorer and other apps see the same files.
- **Names:** no `..`, no absolute paths, no hidden names, no `\` or `:` and no
  control characters; a symbolic link anywhere inside refuses the path, so
  nothing can lead outside the folder.
- **Saving is whole:** a save goes to a staging file that is flushed and then
  renamed over the old file, so a crash leaves the old file or the new one,
  never half of each. Long files are sent in parts and appear only when the
  last part arrives.
- **Trash, not delete:** deleting moves an item into `.Trash` in the folder;
  only "Delete now" in the Trash or "Empty Trash" removes it for good.

## Coming next

In the order they will be built (the plan in the "Built-in apps and toolbox" thread):

| Who | App | Built on |
|---|---|---|
| Everyone | Music and video player with playlists | Photos' playback |
| Everyone | Notes | `kernel/src/apps/notes_app.c` |
| Everyone | Wallet and books | the `ledger.*` methods (ZXV Studio already has a Ledger view) |
| Everyone | Web4 viewer | `kernel/src/browser`, `kernel/src/web4` |
| Everyone | Chat | Commons chat (PR #78) |
| Everyone | Image and voice generation | the Images and voice work (PR #93) |
| Power users | Activity monitor, Settings, Audit log | `swarm.state`, `net.*`, `update.*`, `notes.list`, the quarantine log |
| Developers | Inspector, Packager | `rpc.discover`, `model.info`, `net.peers`; AIPI for signed bundles |
| Developers | Playground | owned by the language examples and templates work |
