<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Release checklist

Tick these in order. Steps marked **owner** publish something or need the
owner's own account, and only the owner does them.

## Before the release pull request

- [ ] Every pull request meant for this release is merged and `main` is
      green (ci, desktop, zxvd, ZXV Studio, sdk*, CodeQL showing pass).
- [ ] **owner** Decide on the old-binaries history purge. It rewrites
      `main`, so do it before the first public release or not at all, and
      before any mirror copies the history.
- [ ] Re-measure speed on the release build (M3 Max and one x86_64 machine)
      and replace the numbers and the TODO note in
      [HUGGING_FACE_PAGE.md](HUGGING_FACE_PAGE.md). The mirror workflow
      refuses to post while a DRAFT or TODO note is there.
- [ ] The default model in `macos/models.manifest` still downloads and
      verifies: `sh macos/zxv-model-fetch.sh macos/models.manifest /tmp/m`
      (about 1.1 GB). CI checks the format with `macos/test_model_fetch.py`
      but does not download it.

## The release pull request

- [ ] Bump `ZXV_VERSION` in `kernel/arch/hosted/zxv_host.c` and `version`
      in `CITATION.cff` to the same value (the release workflow refuses a
      mismatch). Add `date-released` to `CITATION.cff`.
- [ ] Update the README's release-features list if it changed.
- [ ] Merge it.

## Build the draft

- [ ] **owner** Actions, **Release (draft)**, **Run workflow** on `main`,
      version left empty. It builds and checks: Mac `.dmg` and `.app.zip`,
      ZXV Studio `.zip`, Linux `.deb`, `.tar.gz` and AppImage for x86_64 and
      aarch64, the Windows `.zip` (tested on real Windows), the arm64 and
      x86_64 UEFI discs (booted under UEFI firmware), and `SHA256SUMS`.
- [ ] If the AppImage step warned that the upstream runtime changed, check
      the new runtime and pin its hash in `build_system/package_desktop.sh`,
      or release without an AppImage.

## Check the draft by hand

- [ ] Download the Mac `.dmg` on the M3 Max: open it (right-click, Open),
      choose **Download Model**, ask the swarm one question.
- [ ] Open ZXV Studio the same way and look at all five views.
- [ ] Install the `.deb` or run the AppImage on one Linux machine.
- [ ] Run the Windows zip on one Windows 10 or 11 machine.
- [ ] Boot one UEFI disc in QEMU (`docs/BAREMETAL.md`).
- [ ] `sha256sum -c SHA256SUMS` passes on the downloaded files.
- [ ] Release notes read correctly; no model weights are attached.

## Publish

- [ ] **owner** Press **Publish release**. The tag is created at the built
      commit only now.

## Identifiers and mirrors ([MIRRORS.md](MIRRORS.md))

- [ ] **owner** AfricArXiv deposit; then put its DOI in `CITATION.cff` and
      `.zenodo.json` in a pull request.
- [ ] **owner** Zenodo: switch the repository on (once). Later releases are
      archived automatically.
- [ ] **owner** Hugging Face: set the `HF_TOKEN` secret (once), run
      **Mirror a published release (manual)** with dry_run on, read the
      plan, then run it with dry_run off and the tag in **confirm**.
- [ ] **owner** Software Heritage: the same workflow, target
      `software-heritage`.
- [ ] **owner** GitLab pull mirror (once).
- [ ] IPFS and Internet Archive: not yet; only after full debugging and
      compatibility testing.

## Known limits of this release (say so in the notes)

- The Mac app is ad-hoc signed and not notarised; the Windows app is not
  code-signed. Both warn on first launch.
- The UEFI discs are tested under QEMU, not yet on a physical machine.
- SDK packages have their own draft workflow (**SDK release (draft)**,
  `sdk/RELEASING.md`); registry publishing is separate and by hand.
