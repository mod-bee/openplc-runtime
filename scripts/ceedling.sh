#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Autonomy®

# Runs Ceedling (project.yml) with this host's Python flags, taken from
# python3-config, so the suite builds against whatever Python 3 is installed.
#   ./scripts/ceedling.sh test:all
#   ./scripts/ceedling.sh gcov:all
set -euo pipefail

cd "$(dirname "$0")/.."

mixin=build/test/python_mixin.yml
mkdir -p "$(dirname "$mixin")"
{
  echo ":flags:"
  echo "  :test:"
  echo "    :compile:"
  for f in $(python3-config --includes); do echo "      - '$f'"; done
  echo "    :link:"
  for f in $(python3-config --embed --ldflags); do
    case "$f" in -L*) echo "      - '$f'" ;; esac
  done
  # As libraries, so the linker sees them after the objects that need them.
  echo ":libraries:"
  echo "  :test:"
  for f in $(python3-config --embed --libs); do
    case "$f" in -l*) echo "    - '${f#-l}'" ;; esac
  done
} > "$mixin"

exec ceedling --mixin="$mixin" "$@"
