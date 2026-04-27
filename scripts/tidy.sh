#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

JOBS="$(nproc 2>/dev/null || echo 1)"
BUILD_DIR="$PROJECT_ROOT/build/debug"

for arg in "$@"; do
    case "$arg" in
        -j[0-9]*)  JOBS="${arg#-j}" ;;
        --jobs=*)  JOBS="${arg#--jobs=}" ;;
        *)         BUILD_DIR="$arg" ;;
    esac
done

if [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
    echo "Error: compile_commands.json not found in $BUILD_DIR"
    echo "Configure first, e.g.: cmake --preset default"
    exit 1
fi

if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "Error: clang-tidy not found in PATH"
    exit 1
fi

if ! command -v g++ >/dev/null 2>&1; then
    echo "Error: g++ not found in PATH"
    exit 1
fi

# clang-tidy on some setups misses GCC internal C headers (e.g. stddef.h).
GCC_INTERNAL_INCLUDE="$(g++ -print-file-name=include)"

mapfile -t FILES < <(grep -oE '"file"[[:space:]]*:[[:space:]]*"[^"]+"' \
    "$BUILD_DIR/compile_commands.json" \
    | sed -E 's/.*"file"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/' \
    | sort -u)

if [ "${#FILES[@]}" -eq 0 ]; then
    echo "Error: no files found in $BUILD_DIR/compile_commands.json"
    exit 1
fi

echo "Running clang-tidy on ${#FILES[@]} translation units (jobs: $JOBS, build: $BUILD_DIR)"

printf '%s\n' "${FILES[@]}" | xargs -P"$JOBS" -I{} \
    clang-tidy {} \
    -p "$BUILD_DIR" \
    --config-file="$PROJECT_ROOT/.clang-tidy" \
    --extra-arg=-isystem"$GCC_INTERNAL_INCLUDE"

echo "Done."
