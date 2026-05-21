#!/usr/bin/env bash
# scripts/tidy.sh
#
# Run clang-tidy-21 over public headers and implementation files using run-clang-tidy-21.
# Mirrors the CI tidy job exactly so local results match what the PR
# pipeline reports.
#
# Prerequisites:
#   - clang-tidy-21 installed (sudo apt-get install clang-tidy-21)
#   - project configured with the default preset:
#       cmake --preset default
#
# Usage: bash scripts/tidy.sh
set -euo pipefail

if [ ! -f "build/debug/compile_commands.json" ]; then
    echo "Error: build/debug/compile_commands.json not found." >&2
    echo "Configure first: cmake --preset default" >&2
    exit 1
fi

if ! command -v run-clang-tidy-21 >/dev/null 2>&1; then
    echo "Error: run-clang-tidy-21 not found in PATH." >&2
    echo "Install with: sudo apt-get install clang-tidy-21" >&2
    exit 1
fi

repo_root=$(pwd)
file_filter="^${repo_root}/(include/aevox|src)/.*\\.(cpp|hpp)$"

run-clang-tidy-21 -p build/debug "${file_filter}"
