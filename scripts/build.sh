#!/usr/bin/env bash
#
# Configures and builds AlphaEngine with Ninja on Linux / macOS.
#
# Counterpart of scripts/build.ps1 (which handles the Windows toolchains).
# Configures a single-config Ninja build dir, builds every target (the
# engine and, unless the cache says otherwise, the unit tests) and verifies
# that the executable landed in Binaries/. Can be run from any directory;
# the repo root is resolved from the script path.
#
# Usage:
#   scripts/build.sh                                   # Release build into build/
#   scripts/build.sh --config Debug --clean            # wipe build/ first
#   scripts/build.sh --tests                           # also run ctest afterwards
#   scripts/build.sh --build-dir out -- -DCMAKE_CXX_COMPILER=clang++
#
# Options:
#   --config <name>     CMake build type: Debug, Release, RelWithDebInfo or
#                       MinSizeRel. Defaults to Release.
#   --clean             Delete the build directory before configuring.
#   --tests             Configure with -DALPHAENGINE_BUILD_TESTS=ON, build the
#                       AlphaEngineTests target and run ctest.
#   --build-dir <dir>   Override the build directory (default: <repo>/build).
#   --                  Everything after it is passed to the cmake configure
#                       step verbatim (extra -D options, a toolchain file...).

set -euo pipefail

config=Release
clean=0
run_tests=0
build_dir=""
extra_cmake_args=()

usage()
{
    # Print the leading comment block (everything after the shebang up to
    # the first non-comment line) without the comment markers.
    awk 'NR > 1 && !/^#/ { exit } NR > 1 { sub(/^# ?/, ""); print }' "$0"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --config)
            [ $# -ge 2 ] || { echo "error: --config needs a value" >&2; exit 2; }
            config="$2"
            shift
            ;;
        --config=*)
            config="${1#*=}"
            ;;
        --clean)
            clean=1
            ;;
        --tests)
            run_tests=1
            ;;
        --build-dir)
            [ $# -ge 2 ] || { echo "error: --build-dir needs a path" >&2; exit 2; }
            build_dir="$2"
            shift
            ;;
        --build-dir=*)
            build_dir="${1#*=}"
            ;;
        --)
            shift
            extra_cmake_args=("$@")
            break
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

case "${config}" in
    Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    *)
        echo "error: --config must be Debug, Release, RelWithDebInfo or MinSizeRel (got '${config}')" >&2
        exit 2
        ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
if [ ! -f "${repo_root}/CMakeLists.txt" ]; then
    echo "error: CMakeLists.txt not found at ${repo_root}. Is this script in the repo's scripts/ folder?" >&2
    exit 1
fi
if [ -z "${build_dir}" ]; then
    build_dir="${repo_root}/build"
fi

echo "==> Repo root: ${repo_root}"
echo "    Build dir: ${build_dir}"
echo "    Configuration: ${config}"

# --- Tool checks ---
for tool in cmake ninja; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "error: ${tool} not found on PATH." >&2
        exit 1
    fi
done
echo "    CMake: $(command -v cmake) ($(cmake --version | head -n 1))"

# --- Optional clean ---
if [ "${clean}" -eq 1 ] && [ -d "${build_dir}" ]; then
    echo "==> Cleaning ${build_dir}"
    rm -rf "${build_dir}"
    echo "    Removed"
fi

# --- Configure + build ---
config_args=(-S "${repo_root}" -B "${build_dir}" -G Ninja "-DCMAKE_BUILD_TYPE=${config}")
if [ "${run_tests}" -eq 1 ]; then
    config_args+=(-DALPHAENGINE_BUILD_TESTS=ON)
fi
if [ ${#extra_cmake_args[@]} -gt 0 ]; then
    config_args+=("${extra_cmake_args[@]}")
fi

echo "==> Configuring"
cmake "${config_args[@]}"
echo "    Configure succeeded"

echo "==> Building"
cmake --build "${build_dir}"
echo "    Build succeeded"

# --- Verify output ---
echo "==> Verifying output"
expected_exe="${repo_root}/Binaries/AlphaEngine"
if [ ! -f "${expected_exe}" ]; then
    echo "error: expected binary missing: ${expected_exe}" >&2
    exit 1
fi
echo "    Built: ${expected_exe} ($(wc -c < "${expected_exe}" | tr -d ' ') bytes)"

# --- Tests ---
if [ "${run_tests}" -eq 1 ]; then
    echo "==> Building AlphaEngineTests"
    cmake --build "${build_dir}" --target AlphaEngineTests
    echo "==> Running ctest"
    ctest --test-dir "${build_dir}" --output-on-failure
    echo "    Tests passed"
fi

echo
echo "Build complete."
