<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Releasing ZXV

Every release step that leaves this repository is done by the owner by hand.
The workflow only prepares a draft. The full list is
[CHECKLIST.md](CHECKLIST.md); mirrors and DOIs are in [MIRRORS.md](MIRRORS.md).

1. Bump `ZXV_VERSION` in `kernel/arch/hosted/zxv_host.c` (and `version` in
   `CITATION.cff`) in a pull request, and merge it.
2. Actions tab, **Release (draft)**, **Run workflow** on `main`. Leave the
   version empty to use `ZXV_VERSION`.
3. The workflow builds and checks every package: the Mac `.dmg` and
   `.app.zip`, Linux `.deb`, `.tar.gz` and AppImage for x86_64 and aarch64,
   the Windows `.zip`, ZXV Studio's `.zip`, and the arm64 and x86_64 UEFI discs (each booted
   under UEFI firmware first). It writes `SHA256SUMS` and creates a **draft**
   release `v<version>`. It refuses to run if that release or tag exists,
   and refuses any model weights.
4. Review the draft on the Releases page and press **Publish release**. The
   tag is created at the built commit only then.
5. Mirrors (Hugging Face page from [HUGGING_FACE_PAGE.md](HUGGING_FACE_PAGE.md),
   DOI, Zenodo, Software Heritage, GitLab, IPFS) are separate, owner-approved
   steps; the Hugging Face and Software Heritage copies run from the manual
   **Mirror a published release** workflow, dry run first.

Model weights never go in git or in a GitHub release.
