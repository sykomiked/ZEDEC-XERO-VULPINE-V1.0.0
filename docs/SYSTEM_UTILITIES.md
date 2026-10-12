# System utilities

A complete operating system ships the tools to see and run the machine, not
only apps to work in. This page lists every utility ZXV needs, measured
against what macOS and common Linux desktops ship, with the name each one has
in ZXV and what it is built on. The everyday apps (Burrow, Quillon, Glint,
Spindle, Murmuration) are in [STAPLE_APPS.md](STAPLE_APPS.md).

Every utility follows the staple apps' pattern: a toolkit-neutral model in
`kernel/src/apps/` that talks to zxvd ([ZXVD.md](ZXVD.md)). The native
toolkit (`kernel/src/zui`) draws it, and a utility that changes the machine
says so and asks first. The names are ZXV's own. A trademark search is still
needed before release.

## Built

| Utility | Does | Like | zxvd | Model |
|---|---|---|---|---|
| **Almanac** | About this machine: system, CPUs, memory, uptime, the volume holding the ZXV folder, zxvd's own use; AI explains it in plain words | About This Mac, System Information, `neofetch` | `sys.info`, `sys.disk` | `almanac_model.h` |
| **Tidewatch** | Activity monitor: zxvd's CPU share and memory, the machine's memory and load, as a two-minute history to graph | Activity Monitor, `top`, GNOME System Monitor | `sys.info` sampled | `tidewatch_model.h` |
| **Wake** | Event console: follows zxvd's events (refusals as warnings, changes as info), filters by level and text, says how many it missed; history reaches back across restarts a page at a time, and a check shows whether the journal was changed; AI explains the warnings | Console, `journalctl`, Event Viewer | `log.tail`, `journal.query`, `journal.verify` | `wake_model.h` |
| **Sounding** | Network monitor: interfaces, addresses, live bytes a second, zxvd's request traffic | Network Utility, `nload`, `ip -s link` | `sys.net` | `sounding_model.h` |
| **Scent** | Search: finds files in a space by name and by the words inside them as the owner types, best first with the text around each hit, opens each in the right app (Quillon, Glint or Burrow); AI can suggest a sharper search, run only when accepted | Spotlight, GNOME Search, `locate`, `grep -r` | `search.query`, `search.refresh` | `scent_model.h` |
| **Platen** | Printers and scanners on the local network: adds a printer by its address and shows its state, what it takes and why it stopped; prints a file of the space and follows or cancels the job; scans pages from the glass or the feeder into a folder; AI explains a printer's trouble in plain words | Printers & Scanners, CUPS, SANE | `print.status`, `print.submit`, `print.job`, `print.cancel`, `scan.status`, `scan.start` | `platen_model.h` |
| **Keepsake** | Backups with versions: backs a space up to a folder on another disk, lists the backups, shows every version a file has had, checks that a backup reads back whole, brings a file or folder back as a new item (never over what is there), lets old backups go; AI explains a failed check | Time Machine, Déjà Dup, `restic` | `backup.run`, `backup.list`, `backup.versions`, `backup.verify`, `backup.restore`, `backup.prune` | `keepsake_model.h` |
| **Spindle** | Developer console: any zxvd call by hand | Script Editor, `curl` | `rpc.discover` + any | `spindle_model.h` |

Only Platen and Keepsake change anything. Platen sends a document to a
printer and saves scanned pages as new files. Keepsake writes backups to the
destination the owner picks, restores as new items, and removes old backups
only when the owner asks. `sys.*`, `log.tail`, `journal.*` and `search.*` are read only.
All of them are local to the owner's control socket and never sent to peers. The log keeps what
happened (a method, an outcome, how long it took), never parameters or
content.

Scent's index (`kernel/arch/hosted/zxv_search.h`) walks a space through the
same rules as Burrow, so it never sees hidden files, the Trash or links. It
keeps each file's words and their prefixes (from 2 to 24 bytes, so "harv"
finds "harvest"), reads again only the files whose size or time changed, and
holds up to 20,000 files per space and the first 1 MiB of each text file.
Each space has its own index, and four are kept at once. A file elsewhere on
the disk is not searched: the ZXV folder is the one place apps see.

Platen speaks IPP Everywhere to printers and eSCL to scanners
(`kernel/src/print`, `kernel/arch/hosted/zxv_print_host.h`), the driverless
protocols almost every network printer and scanner made since 2015 answers.
It only reaches addresses on the local network (loopback, private, link-local,
IPv6 unique-local), so zxvd never becomes a way to send documents to the
internet. A document goes only in a format the printer says it takes (PDF,
JPEG, PNG, PWG or Apple raster, plain text); ZXV does not convert documents
yet. Scanned pages are saved as "Scan N" in the chosen folder, and pages saved
before a failure are kept. Encrypted printers (`ipps://`) wait for a TLS
client in the hosted layer, and finding printers by themselves (DNS-SD)
comes later; for now the owner types the address.

Keepsake (`kernel/arch/hosted/zxv_keepsake.h`) keeps each file's bytes once,
named by their SHA-256, under "ZXV Keepsake" in the folder the owner picks,
so a file that did not change costs nothing in the next backup and many
versions fit in little space. Each backup is a snapshot: a list of every
file with its hash, ending in a line that carries the hash of the list, so a
damaged snapshot is never trusted. New copies are read back before the
snapshot that names them is written, and a check reads every copy a
snapshot names: a backup counts as tested only when that passes. A full
backup reads every file again and repairs any copy that no longer matches.
Restoring never overwrites: the item comes back where it was if that is
free, or beside it as "name (restored).ext". Removing old backups is refused
while any snapshot is damaged, so nothing it might need is lost. Backups are
not encrypted yet; that waits for Lockbox's keys.

Wake's history comes from the journal (`kernel/arch/hosted/zxv_journal.h`):
every event zxvd logs, written to disk as it happens, so it outlives a
restart. Each line carries a SHA-256 hash of the line before it and itself,
so a line edited or removed afterwards shows when Wake checks the journal,
which names the first line that no longer fits. The journal lives in a
folder only the owner can read, in files of up to 1 MiB, and the oldest go
once all of them pass 16 MiB. A line cut short by a crash is left out and
the chain carries on from the last whole one. `zxvd --desktop` and the Mac
app keep it beside the books file; plain `zxvd` keeps one only with
`--journal DIR`.

## Still to build

In build order, highest first. "Built on" names what exists today.

| Utility | Does | Like | Built on |
|---|---|---|---|
| **Keystone** | Settings: displays, sound, keyboard, language, accessibility, network, sharing, the ZXV folder, model choice, budgets | System Settings, GNOME Settings | `theme`, `display`, `audio`, `keyboard`, `i18n`, `wifi`, `bluetooth` modules; needs a `settings.*` store in zxvd |
| **Tidewatch, part 2** | Every process on the machine and the swarm's agents, with quit and priority | Activity Monitor's process list, `htop` | `swarm.state`; host process listing (`/proc`, libproc, Toolhelp) |
| **Sounding, part 2** | Peers and mesh links, PQ sessions, per-peer traffic, firewall view | Little Snitch view, `nethogs`, `ss` | `net.peers`, `mesh.books`, `mesh_net` |
| **Cellar** | Disk utility: volumes, space by folder and space, Trash size, check and repair the ZXV folder, disc images | Disk Utility, GNOME Disks, `ncdu` | `sys.disk`, `files.*`, `zxvfs`, `blockdev` |
| **Keepsake, part 2** | Encrypted backups (with Lockbox's keys), to a peer or IPFS, on a schedule (with Treadle) | Time Machine to a network disk | `ipfs_node`, `svault`, `event_sched` |
| **Witness** | Audit log: signed record of what changed, who approved it, held crossings, AI actions accepted | Console's audit view, `auditd` | `chronicle`, `provenance`, `peer_audit` |
| **Gatehouse** | Permissions and privacy: which app may use files, camera, microphone, network, which capabilities each holds | Privacy & Security, Flatpak permissions | `zxv_cap` capabilities, `onepolicy` |
| **Lockbox** | Keys and passwords: ML-DSA identity keys, sealed hardware keys, saved passwords | Keychain Access, Seahorse | `pqsec`, `identity`, retail hardware keys |
| **Tideturn** | Software updates: dual-signed OS and app updates with the audit trail | Software Update, GNOME Software updates | `update.check`, `update`, `hwemu` dual-signed updates |
| **Satchel** | Packager: build, sign and install ZXV packages | Installer, `dpkg`, Flatpak | `zxpkg`, AIPI signed bundles |
| **Lodestone** | Inspector for developers: methods, models, peers, capabilities | Instruments, `d-feet` | `rpc.discover`, `model.info`, `net.peers` |
| **Lifeboat** | Recovery: start from a safe state, repair, reinstall, restore from Keepsake | Recovery mode, GRUB rescue | bare-metal UEFI discs, `zxvfs` journal |
| **Ferry** | Move to a new machine: files, spaces, keys (with the owner's approval), settings | Migration Assistant | `files.cross`, `spaces.*`, Keepsake |
| **Hamper** | Archives: open and make compressed archives | Archive Utility, File Roller | `codec`, `files.*` |
| **Glimpse** | Screenshots and screen recording | Screenshot, GNOME Screenshot | `display`, `video` |
| **Tally** | Calculator with integer and exact rational modes, units and currency | Calculator | `rational`, the economy's FX rates |
| **Typecase** | Fonts: preview, install, check scripts covered | Font Book | `font` registry |
| **Runebook** | Character viewer: every character, emoji and symbol, by script | Character Viewer, GNOME Characters | `font`, `i18n` |
| **Sundial** | Clock, alarms, timers, world time | Clock | `clock`, `timer` |
| **Treadle** | Automation: run a task on a schedule or an event, with the owner's limits | Shortcuts, Automator, `cron` | `event_sched`, swarm budgets |

The terminal (**zxsh**) is owned by the "Paraconsistent terminal and OS gaps"
work; it sits beside these as the command-line way to do the same things.
Screens for all of these are drawn by the native toolkit ("Native app
toolkit build").

## How each one is built

1. **zxvd first.** A read-only method gives the facts (as `sys.*` and
   `log.tail` do). A method that changes something checks the caller's rights
   and is logged, so Wake and Witness show it.
2. **The model.** A C model in `kernel/src/apps/<name>_model.{h,c}` holds
   the state a view draws, takes actions, and holds AI answers until the
   owner accepts them (`zxm.h` M3). No floats: shares are in hundredths of a
   percent and rates in whole units a second.
3. **Tests.** `kernel/src/apps/test_apps.sh` runs each model against zxvd's
   real dispatcher, plus canned replies for exact numbers and damaged input.
4. **The screen.** The native toolkit draws the model. Long actions report
   real progress (`zxm_link_progress`) to an ordinary green and gold bar.
