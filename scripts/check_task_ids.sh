#!/usr/bin/env bash
set -euo pipefail

violations=()

while IFS= read -r path; do
    case "${path}" in
        Tasks/*|ProductRequirement/*)
            continue
            ;;
    esac

    base="$(basename "${path}")"
    if [[ "${base}" =~ AEV-[0-9]{3} ]]; then
        violations+=("${path}")
    fi
done < <(git ls-files -co --exclude-standard)

if ((${#violations[@]} > 0)); then
    printf 'Task ID prefixes are only allowed inside Tasks/ artifacts:\n' >&2
    printf '  %s\n' "${violations[@]}" >&2
    exit 1
fi

printf 'Task ID filename check passed.\n'
