<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Mirrors and identifiers

GitHub is the source of truth. Every other copy is made from a **published**
GitHub release, by the owner, one at a time. Nothing here runs on its own.

| Where | What goes there | How | Set up once |
|---|---|---|---|
| AfricArXiv | the project's original DOI | deposit by hand on AfricArXiv | an AfricArXiv account |
| Zenodo | a DOI per release, archived source | automatic on each published release once switched on | log in at zenodo.org with GitHub, switch this repository on under GitHub settings; metadata comes from `.zenodo.json` |
| Hugging Face | release assets, model card, redistributable model weights | Actions, **Mirror a published release (manual)**, target `huggingface` | create a write token on huggingface.co and save it as the repository secret `HF_TOKEN`; choose the `OWNER/NAME` page |
| Software Heritage | permanent archive of the whole git history | same workflow, target `software-heritage` | nothing |
| GitLab | a read-only copy of the repository | GitLab pull mirror | see below |
| IPFS (Filebase) and Internet Archive | release assets, content-addressed | later | only after full debugging and compatibility testing, both together |

## Order

1. Publish the GitHub release (see [CHECKLIST.md](CHECKLIST.md)).
2. AfricArXiv first: its DOI is the original identifier. Add it to
   `CITATION.cff` (`doi:` and `identifiers:`) and to `.zenodo.json`
   (`related_identifiers`, relation `isIdenticalTo`) in a pull request.
3. Zenodo, Hugging Face and Software Heritage as further identifiers. Run
   the mirror workflow with **dry_run** on first, read what it would send,
   then run it again with dry_run off and the tag typed into **confirm**.

## GitLab pull mirror

GitLab pulls from GitHub, so no GitLab credential is ever stored on GitHub.

1. On gitlab.com: **New project**, **Import project**, **Repository by
   URL**, and enter
   `https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0.git`. Make it
   public and give it the same name.
2. In the new project: **Settings**, **Repository**, **Mirroring
   repositories**: add the same URL, direction **Pull**, and tick **Only
   mirror protected branches**. GitLab then refreshes it about every 30
   minutes. (Pull mirroring needs GitLab Premium or a public project in
   GitLab's open-source programme; without it, re-import by hand per release.)
3. In **Settings**, **CI/CD**, turn CI/CD off for the mirror. This repository
   has no `.gitlab-ci.yml` on purpose: GitHub Actions is the only CI.
4. Put a line at the top of the GitLab project description: "Read-only
   mirror of https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0; open
   issues and pull requests there."

GitCode (for readers in China) can import the same URL the same way.

## What never goes anywhere

- Model weights in git, or in a GitHub release. The Hugging Face page holds
  only weights whose licence allows redistribution and commercial use, each
  with its upstream licence file beside it.
- Hardware design files (they stay on the owner's Mac until the owner
  decides otherwise).
- The Hackronomicon book (all rights reserved; reference only).
