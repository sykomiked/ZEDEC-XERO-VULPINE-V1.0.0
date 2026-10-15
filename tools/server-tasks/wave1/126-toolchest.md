# Wave 1 task: Toolchest (slot 126), SDK kits
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 126, build position 2 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Tally (layer 109, wave 1, an earlier wave: `use tally 1`); Ninefold (layer 80, wave 1, a later wave (a cycle): `use ninefold 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 8 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: the SDK kits (sdk/kits, the language SDKs) with what each needs; daemon version from `zxvd.version`, its methods from `rpc.discover`, samples from `play.catalogue`, update check with `update.check`. zxvd has no kit-install method: use only existing methods; list what's missing in your report.
Logic (critical): kit fits this zxvd (major.minor.patch compare) in sutra + kron witnessed; "may install" = both signatures valid AND fits AND size at most free space, in eve + kron witnessed; download and installed sizes exact in trebago; kit record (name, version, language, size) in myra.
Never install anything in tests.
needs: sys.read, net.
Planned (no runtime yet): Raku, Haskell, Bash.
