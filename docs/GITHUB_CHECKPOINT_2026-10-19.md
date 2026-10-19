# Cloud development checkpoint, 19 October 2026

This checkpoint preserves the cloud workspace based on Claude-branch commit
`b567ac1ea0b89ff78645f3dc37d519a8024119fd`. It is separate from newer changes
already on GitHub and from the Mac-local Devin deployment workspace. It must
not replace either of those trees wholesale.

## Source and binary provenance

The source branch contains the QA, logic, arithmetic, fee, COBOL, agent-manifest
and bounded dividend changes described in `QA_PROGRESS.md`. The associated
prerelease contains the five architecture ELF kernels, available raw kernel
binaries, source archive, checksums, build manifest and validation logs. Generated
binaries remain outside Git history, as required by the repository's ignore
rules. They are development artifacts, not a production release or a complete
implementation of every advertised subsystem.

Publication validation reruns the focused QA gate and existing ten-second QEMU
boot gate for all five architectures. The earlier full host gate passed on
10 October; its temporary log did not survive the environment restart, so it
is not presented as a fresh full-suite run on 19 October. The prerelease logs
record checks actually rerun for publication. Checksums detect byte changes;
they do not constitute a release signature or an independent security audit.

## Devin and IPFS reconciliation

The supplied manual is a reference document, not executable instructions. Its
front matter identifies an 18 October H100 archive and compartment index
`QmdYhdsyt3mwEuh12tDYWN8RJHH25eBKhkNMkwZxknLh3c`. The two listed gateways
returned HTTP 403 from this environment. The archive and the nine-server
deployment have therefore not been imported or verified here.

To reconcile the Devin workspace without losing its progress:

1. Identify the actual source checkout, branch, HEAD and working-tree changes on
   the Mac. Preserve tracked edits and intentional new source files on a new
   branch. Exclude private keys, credentials, local configuration, runtime state
   and generated artifacts from the source commit.
2. Push that branch normally; do not force-push the cloud checkpoint or overwrite
   the current main branch. Supply its URL so the source trees can be compared
   and integrated with their history intact.
3. Export the deployed ELF/image artifacts with checksums, exact source commit,
   toolchain versions, architecture, build commands and boot/conformance evidence.
   Preserve any difference between built, deployed and source-committed versions.
   Upload binaries as release assets tied to that source commit.
4. If IPFS is the transfer route, provide a reachable gateway or an export of the
   index and required source/build objects. Verify the referenced objects before
   treating them as the newest source. The manual alone is not a source archive.

PR #156 is already merged into main. This checkpoint does not modify that PR,
merge unrelated changes into main or claim to update the running server fleet.
