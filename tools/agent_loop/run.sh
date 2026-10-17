#!/bin/sh
# run.sh — the A6000 coding engine, one pass over the queue.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
set -eu
cd "$(dirname "$0")"
exec python3 run_tasks.py --once "$@"