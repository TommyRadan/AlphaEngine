#!/usr/bin/env bash
#
# Checks or fixes C++ formatting against the repo's .clang-format.
#
# Linux / macOS counterpart of scripts/check-style.ps1: enumerates the
# project source files (*.cpp, *.hpp, *.h) under runtime/, core/,
# rendering_engine/ and external/ -- the same scope CI's clang-format job
# covers -- skipping vendored code and build output. By default runs
# clang-format in dry-run mode and fails on any diff; with --fix, rewrites
# the files in place.
#
# Usage:
#   scripts/check-style.sh                       # check only (exit 1 on issues)
#   scripts/check-style.sh --fix                 # reformat in place
#   scripts/check-style.sh --clang-format /path/to/clang-format
#
# The tool is resolved from --clang-format, then the CLANG_FORMAT
# environment variable, then clang-format-18 (the version CI pins) and
# finally an unsuffixed clang-format on PATH.

set -euo pipefail

fix=0
clang_format="${CLANG_FORMAT:-}"

usage()
{
    # Print the leading comment block (everything after the shebang up to
    # the first non-comment line) without the comment markers.
    awk 'NR > 1 && !/^#/ { exit } NR > 1 { sub(/^# ?/, ""); print }' "$0"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --fix)
            fix=1
            ;;
        --clang-format)
            [ $# -ge 2 ] || { echo "error: --clang-format needs a path" >&2; exit 2; }
            clang_format="$2"
            shift
            ;;
        --clang-format=*)
            clang_format="${1#*=}"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown argument '$1'" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
if [ ! -f "${repo_root}/.clang-format" ]; then
    echo "error: .clang-format not found at ${repo_root}" >&2
    exit 1
fi

# --- Locate clang-format ---
if [ -z "${clang_format}" ]; then
    for candidate in clang-format-18 clang-format; do
        if command -v "${candidate}" >/dev/null 2>&1; then
            clang_format="${candidate}"
            break
        fi
    done
fi
if [ -z "${clang_format}" ] || ! command -v "${clang_format}" >/dev/null 2>&1; then
    echo "error: clang-format not found. Install LLVM 18 or pass --clang-format <path>." >&2
    exit 1
fi
echo "==> Using ${clang_format} ($("${clang_format}" --version | head -n 1))"

# --- Enumerate source files ---
cd "${repo_root}"
source_dirs=(runtime core rendering_engine external)
existing_dirs=()
for d in "${source_dirs[@]}"; do
    [ -d "${d}" ] && existing_dirs+=("${d}")
done
if [ ${#existing_dirs[@]} -eq 0 ]; then
    echo "error: no source directories found under ${repo_root}" >&2
    exit 1
fi
files=()
while IFS= read -r f; do
    files+=("${f}")
done < <(find "${existing_dirs[@]}" -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) | sort)
if [ ${#files[@]} -eq 0 ]; then
    echo "error: no source files found." >&2
    exit 1
fi
echo "    ${#files[@]} file(s) under ${source_dirs[*]}"

# --- Run clang-format ---
if [ "${fix}" -eq 1 ]; then
    echo "==> Fixing in place"
    printf '%s\n' "${files[@]}" | xargs "${clang_format}" --style=file -i --
    echo "    All files reformatted"
    exit 0
fi

echo "==> Checking (dry-run)"
bad=()
for f in "${files[@]}"; do
    if ! "${clang_format}" --style=file --dry-run --Werror -- "${f}" 2>&1; then
        bad+=("${f}")
    fi
done
if [ ${#bad[@]} -eq 0 ]; then
    echo "    No style issues."
    exit 0
fi

echo
echo "Style issues in ${#bad[@]} file(s):"
printf '  - %s\n' "${bad[@]}"
echo
echo "Run scripts/check-style.sh --fix to apply formatting."
exit 1
