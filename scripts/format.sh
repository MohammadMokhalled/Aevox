#!/usr/bin/env bash
set -euo pipefail

if ! command -v clang-format &>/dev/null; then
    echo "Error: clang-format not found. Install with: sudo apt-get install clang-format" >&2
    exit 1
fi

FILES=$(find include src tests examples -name '*.hpp' -o -name '*.cpp' 2>/dev/null | sort)
COUNT=$(echo "$FILES" | grep -c . || true)

echo "Formatting $COUNT file(s)..."
echo "$FILES" | xargs clang-format -i

echo "Verifying formatting..."
echo "$FILES" | xargs clang-format --dry-run --Werror

echo "All $COUNT file(s) formatted and verified."
