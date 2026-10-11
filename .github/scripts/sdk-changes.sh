#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Decides which SDK jobs in .github/workflows/sdk.yml run, so a pull request
# spends runners only on what it changed. An area runs when its folder
# exists and either the area changed or something every SDK builds on did
# (libzxv, the conformance vectors, the kernel sources, this workflow).
# Pushes and manual runs test every area that exists.
#
# Usage: sdk-changes.sh [BASE_SHA]   (writes key=true|false lines to
# $GITHUB_OUTPUT, or to stdout when that is unset)
set -euo pipefail

base="${1:-}"
out="${GITHUB_OUTPUT:-/dev/stdout}"

all=false
if [ -z "$base" ]; then
  all=true
  changed=""
else
  changed="$(git diff --name-only "$base" HEAD)"
  if printf '%s\n' "$changed" | grep -qE '^(sdk/libzxv/|sdk/conformance/|kernel/src/|kernel/third_party/|\.github/workflows/sdk\.yml$|\.github/scripts/sdk-changes\.sh$)'; then
    all=true
  fi
fi

# key  folder
areas='
docs        sdk/docs
libzxv      sdk/libzxv
python      sdk/python
java        sdk/java
dotnet      sdk/dotnet
typescript  sdk/typescript
solidity    sdk/solidity
ruby        sdk/ruby
php         sdk/php
rust        sdk/rust
go          sdk/go
cpp         sdk/cpp
swift       sdk/swift
kits        sdk/kits
'

while read -r key dir; do
  [ -n "$key" ] || continue
  run=false
  if [ -d "$dir" ]; then
    if [ "$all" = true ] || printf '%s\n' "$changed" | grep -q "^$dir/"; then
      run=true
    fi
  fi
  echo "$key=$run" >> "$out"
  echo "$key=$run" >&2
done <<< "$areas"
