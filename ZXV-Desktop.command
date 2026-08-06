#!/usr/bin/env bash
# Double-click to boot the ZEDEC pqOS graphical desktop.
cd "$(dirname "$0")" || exit 1
exec bash build_system/run_desktop.sh
