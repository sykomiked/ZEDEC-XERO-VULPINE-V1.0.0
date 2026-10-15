# Wave 10 task: Toolchest (slot 126), SDK kits
Read ../APP_WAVE_SPEC.md first.
What it does: the SDK kits (sdk/kits, the language SDKs) with what each needs; daemon version from `zxvd.version`, its methods from `rpc.discover`, samples from `play.catalogue`, update check with `update.check`. zxvd has no kit-install method: use only existing methods; list what's missing in your report.
Logic (critical): kit fits this zxvd (major.minor.patch compare) in sutra + kron witnessed; "may install" = both signatures valid AND fits AND size at most free space, in eve + kron witnessed; download and installed sizes exact in trebago; kit record (name, version, language, size) in myra.
Never install anything in tests.
needs: sys.read, net.
Planned (no runtime yet): Raku, Haskell, Bash.
