# curzi8889/tap — the ZXV CLI suite

`brew tap curzi8889/tap && brew install zxv 36n9 9n63`

- `zxv` — the node CLI: daemon, telemetry, bank resolution, CISEX search.
- `36n9` — the generative plane: `36n9://` resolution, bank slices.
- `9n63` — the state plane: telemetry, spaces, ledger.

Every tool answers `--ai-manifest` with its full command tree in JSON,
so autonomous agents can discover and drive the suite without parsing
human text. The formulas verify the release tarball's sha256; the
release pipeline publishes the statically linked binaries per tag.
