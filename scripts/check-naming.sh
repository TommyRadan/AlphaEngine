#!/usr/bin/env bash
#
# Checks or fixes C++ identifier naming against .clang-tidy (snake_case).
#
# Linux / macOS counterpart of scripts/check-naming.ps1: runs clang-tidy's
# readability-identifier-naming check over the first-party sources
# (runtime/, core/, rendering_engine/, external/). A separate build dir
# (build-tidy/ by default) is configured with Ninja and
# CMAKE_EXPORT_COMPILE_COMMANDS=ON so a compile_commands.json is available;
# pass --build-dir to reuse a build dir that was already configured that way.
# Vendored code under vendor/ and build output are not touched.
#
# By default violations are reported and the script exits 1 if any are
# found; with --fix, renames are applied in place.
#
# Usage:
#   scripts/check-naming.sh                        # check (exit 1 on violations)
#   scripts/check-naming.sh --fix                  # apply renames in place
#   scripts/check-naming.sh --build-dir build      # reuse an existing compile db
#   scripts/check-naming.sh --clang-tidy /path/to/clang-tidy
#
# Tools are resolved from --clang-tidy / CLANG_TIDY, then the -18 suffixed
# binaries CI pins (clang-tidy-18, run-clang-tidy-18,
# clang-apply-replacements-18), then the unsuffixed names on PATH. The check
# runs through run-clang-tidy (parallel, one job per translation unit, exactly
# as CI does) when it is available, and falls back to a single serial
# clang-tidy invocation otherwise. Fixes always go through one serial
# clang-tidy invocation so cross-file renames stay consistent.

set -euo pipefail

fix=0
clang_tidy="${CLANG_TIDY:-}"
build_dir=""

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
        --clang-tidy)
            [ $# -ge 2 ] || { echo "error: --clang-tidy needs a path" >&2; exit 2; }
            clang_tidy="$2"
            shift
            ;;
        --clang-tidy=*)
            clang_tidy="${1#*=}"
            ;;
        --build-dir)
            [ $# -ge 2 ] || { echo "error: --build-dir needs a path" >&2; exit 2; }
            build_dir="$2"
            shift
            ;;
        --build-dir=*)
            build_dir="${1#*=}"
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
if [ ! -f "${repo_root}/.clang-tidy" ]; then
    echo "error: .clang-tidy not found at ${repo_root}" >&2
    exit 1
fi
cd "${repo_root}"

# --- Locate clang-tidy (and its helpers) ---
find_tool()
{
    # $1: explicit path (may be empty); rest: candidate names in priority order.
    local explicit="$1"
    shift
    if [ -n "${explicit}" ]; then
        command -v "${explicit}" >/dev/null 2>&1 && echo "${explicit}"
        return 0
    fi
    local candidate
    for candidate in "$@"; do
        if command -v "${candidate}" >/dev/null 2>&1; then
            echo "${candidate}"
            return 0
        fi
    done
    return 0
}

clang_tidy="$(find_tool "${clang_tidy}" clang-tidy-18 clang-tidy)"
if [ -z "${clang_tidy}" ]; then
    echo "error: clang-tidy not found. Install LLVM 18 or pass --clang-tidy <path>." >&2
    exit 1
fi
echo "==> Using ${clang_tidy} ($("${clang_tidy}" --version | grep -o 'version [0-9.]*' | head -n 1))"

# run-clang-tidy is optional: it parallelises the check. Prefer the helper
# that matches the resolved clang-tidy (a -18 suffix or none) so the two
# never come from different LLVM installs.
run_clang_tidy=""
if [ "${fix}" -eq 0 ]; then
    case "${clang_tidy}" in
        *-18) run_clang_tidy="$(find_tool "" run-clang-tidy-18 run-clang-tidy)" ;;
        *)    run_clang_tidy="$(find_tool "" run-clang-tidy run-clang-tidy-18)" ;;
    esac
fi

# --- Configure compile_commands.json via Ninja ---
if [ -z "${build_dir}" ]; then
    build_dir="${repo_root}/build-tidy"
fi
compile_db="${build_dir}/compile_commands.json"
if [ ! -f "${compile_db}" ]; then
    echo "==> Configuring ${build_dir} (Ninja) to generate compile_commands.json"
    if ! command -v ninja >/dev/null 2>&1; then
        echo "error: ninja not found on PATH (needed to export compile_commands.json)." >&2
        exit 1
    fi
    cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi
if [ ! -f "${compile_db}" ]; then
    echo "error: compile_commands.json was not generated at ${compile_db}" >&2
    exit 1
fi
echo "    compile_commands.json: ${compile_db}"

# --- Enumerate project source files ---
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

# --- Run clang-tidy ---
# clang-tidy exits 0 when it only reports warnings (WarningsAsErrors is
# empty in .clang-tidy), so pass/fail is decided by scanning the output
# for naming diagnostics, exactly as check-naming.ps1 does.
output_file="$(mktemp)"
trap 'rm -f "${output_file}"' EXIT
exit_code=0

if [ "${fix}" -eq 1 ]; then
    echo "==> Applying identifier renames"
    # One invocation over every file so cross-file renames stay consistent.
    "${clang_tidy}" -p "${build_dir}" --quiet --fix "${files[@]}" 2>&1 | tee "${output_file}" || exit_code=${PIPESTATUS[0]}
    if [ "${exit_code}" -ne 0 ]; then
        echo "    clang-tidy reported issues during fix (exit ${exit_code})."
    else
        echo "    Renames applied."
    fi
    exit "${exit_code}"
fi

echo "==> Checking identifier naming"
if [ -n "${run_clang_tidy}" ]; then
    # Same invocation as CI: every translation unit under the four source
    # directories, in parallel; headers are covered through HeaderFilterRegex.
    # The regex is anchored to the repo root so vendored sources that live
    # under a like-named subdirectory (e.g. SDL3's src/core/) are excluded.
    pattern="${repo_root}/(runtime|core|rendering_engine|external)/"
    "${run_clang_tidy}" -clang-tidy-binary "${clang_tidy}" -p "${build_dir}" -quiet "${pattern}" 2>&1 \
        | tee "${output_file}" || exit_code=${PIPESTATUS[0]}
else
    "${clang_tidy}" -p "${build_dir}" --quiet "${files[@]}" 2>&1 | tee "${output_file}" || exit_code=${PIPESTATUS[0]}
fi

violations=$(grep -c 'warning: invalid case' "${output_file}" || true)
if [ "${exit_code}" -eq 0 ] && [ "${violations}" -eq 0 ]; then
    echo "    No naming violations."
    exit 0
fi

echo
if [ "${violations}" -ne 0 ]; then
    echo "${violations} naming violation(s) reported."
fi
echo "Run scripts/check-naming.sh --fix to apply renames."
if [ "${exit_code}" -ne 0 ]; then
    exit "${exit_code}"
fi
exit 1
