#!/bin/bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ci_changes.sh — decide which CI jobs a change needs. Prints key=value lines
# (and appends them to $GITHUB_OUTPUT when set). Every flag is "true" or
# "false". Used by the quick-checks job in .github/workflows/ci.yml and by
# scripts/verify-local.sh.
#
#   build_system/ci_changes.sh <base>
#
#   base   commit to diff HEAD against; empty, all zeros or unknown means
#          "everything changed" (new branch, nightly, manual run)
#
# Environment (all optional):
#   EVENT  GitHub event name: push, pull_request, schedule, workflow_dispatch
#   DRAFT  "true" when the pull request is a draft
#   REF    the pushed ref (github.ref); unused since pushes to main went
#          back to the fast tier (the nightly run covers main's slow tier)
#   WORKTREE  "1" to diff the working tree (uncommitted and untracked files
#          included) instead of HEAD; scripts/verify-local.sh sets it
#
# Flags:
#   kernel  kernel/tests/fuzz/bench/build_system or anything not listed below
#   dotnet  kernel or sdk/dotnet changed (the SDK links libzxv and the kernel)
#   sdk     kernel, sdk/ or a bindings/<language>/ directory changed
#   mac     kernel, macos/ or docs/MAC_APP.md (bundled into the app) changed
#   proofs  proofs/ changed
#   heavy   run the slow tier: nightly, manual runs, and pull requests that
#           are ready for review (not drafts). Draft pushes and every branch
#           push, main included, run the fast tier only: a merge to main has
#           already passed the slow tier as a ready pull request, and with a
#           20-job limit a full rerun on main held every open pull request. Merging needs the
#           ci-gate check, which only a ready pull request's run reports and
#           only after the slow tier passed (docs/CI.md, "Merge gate").
#
# Only documentation is skipped: *.md outside docs/MAC_APP.md, docs/, LICENSE,
# NOTICE, FUNDING.yml, CITATION.cff, issue templates, dependabot and CodeQL
# config. A change to ci.yml or to this script runs everything; other
# workflows (sdk*.yml, zxvd.yml, codeql.yml) do not affect this one.
set -euo pipefail
BASE=${1:-}
EVENT=${EVENT:-push}
DRAFT=${DRAFT:-false}
REF=${REF:-}

kernel=false dotnet=false sdk=false mac=false proofs=false all=false

case "$EVENT" in schedule|workflow_dispatch) all=true ;; esac
if [ "$all" = false ]; then
    if [ -z "$BASE" ] || [ -z "${BASE//0/}" ] || ! git cat-file -e "$BASE^{commit}" 2>/dev/null; then
        all=true
    fi
fi

if [ "$all" = false ]; then
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        case "$f" in
            .github/workflows/ci.yml|build_system/ci_changes.sh) all=true ;;
            # other workflows and their scripts test themselves
            .github/workflows/*|.github/scripts/*) ;;
            docs/MAC_APP.md|macos/*) mac=true ;;
            proofs/*) proofs=true ;;
            # libzxv (the C ABI) and its conformance vectors are linked into
            # the hosted engine and zxvd, so they count as kernel code.
            sdk/libzxv/*|sdk/conformance/*) kernel=true ;;
            sdk/dotnet/*) dotnet=true; sdk=true ;;
            sdk/*) sdk=true ;;
            # bindings/*.c and *.h are built by kernel verify-all; only the
            # per-language subdirectories are SDK-only.
            bindings/*/*) sdk=true ;;
            docs/*|*.md|LICENSE|NOTICE|FUNDING.yml|CITATION.cff) ;;
            .github/ISSUE_TEMPLATE/*|.github/dependabot.yml|.github/codeql/*) ;;
            *) kernel=true ;;
        esac
    done < <(if [ "${WORKTREE:-0}" = 1 ]; then
                 git diff --name-only "$BASE"; git ls-files --others --exclude-standard
             else
                 git diff --name-only "$BASE" HEAD
             fi)
fi

if [ "$all" = true ]; then
    kernel=true dotnet=true sdk=true mac=true proofs=true
fi
if [ "$kernel" = true ]; then
    dotnet=true sdk=true mac=true
fi

heavy=false
case "$EVENT" in
    schedule|workflow_dispatch) heavy=true ;;
    pull_request) [ "$DRAFT" = true ] || heavy=true ;;
esac

out="kernel=$kernel
dotnet=$dotnet
sdk=$sdk
mac=$mac
proofs=$proofs
heavy=$heavy"
echo "$out"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
    echo "$out" >> "$GITHUB_OUTPUT"
fi
