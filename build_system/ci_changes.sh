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
#   WORKTREE  "1" to diff the working tree (uncommitted and untracked files
#          included) instead of HEAD; scripts/verify-local.sh sets it
#
# Flags:
#   kernel  kernel/tests/fuzz/bench/build_system or anything not listed below
#   dotnet  kernel or bindings/dotnet changed (the shim links kernel sources)
#   sdk     kernel or a bindings/<language>/ directory changed
#   mac     kernel, macos/ or docs/MAC_APP.md (bundled into the app) changed
#   proofs  proofs/ changed
#   heavy   run the slow tier: nightly, manual runs, and pull requests that
#           are ready for review (not drafts). Draft pushes and pushes to a
#           branch run the fast tier only; nothing is merged without the slow
#           tier, because a draft has to be marked ready first.
#
# Only documentation is skipped: *.md outside docs/MAC_APP.md, docs/, LICENSE,
# NOTICE, FUNDING.yml, CITATION.cff, issue templates, dependabot and CodeQL
# config. A change to a workflow or to this script runs everything.
set -euo pipefail
BASE=${1:-}
EVENT=${EVENT:-push}
DRAFT=${DRAFT:-false}

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
            .github/workflows/*|build_system/ci_changes.sh) all=true ;;
            docs/MAC_APP.md|macos/*) mac=true ;;
            proofs/*) proofs=true ;;
            bindings/dotnet/*) dotnet=true; sdk=true ;;
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
