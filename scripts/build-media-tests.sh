#!/bin/bash

# Builds and runs the tests of hatn's `media` module through hatn's own CMake and ctest. Optional and
# separate from build-hatnuise.sh, because hatn builds its tests only when it is the master
# (top-level) project. See scripts/README.md.
#
# Usage: build-media-tests.sh [debug|release|minsize_release] [options]
#
#   --module <name>     hatn DEV_MODULE (default media; `all` builds every hatn module, see below).
#   --require-codec     fail unless libopus and libogg are installed in the deps root. Without them
#                       only TestPcmRing and TestWaveform have any content: the other suites
#                       compile to empty shells and "pass".
#   --no-run            configure and build, do not run ctest
#   --clean             delete the build directory first
#   --jobs <n>          parallel jobs (default: $build_workers, else 6)
#   --dry-run           resolve and print everything, run nothing (also: PRINT_ONLY=1)
#   -h, --help          this text
#
# The default is `media`: hatn builds media plus the five modules of media/depends.cmake (`common
# validator base logcontext dataunit`) and media's six tests. That file used to list only `common base
# logcontext`, which left dataunit and validator (needed by base) unbuilt with DEV_MODULE=media; it was
# fixed on 2026-09-20 and the fixed configuration was run and passed. `--module all` still works, but each
# test executable then links every hatn module, so its first run builds all of hatn.
#
# The build type defaults to debug. The environment is the one build-hatnuise.sh reads (hatn_src,
# deps_universal_root, deps_root, boost_version, hatn_plugins, PYTHON_EXE, build_workers,
# hatnuise_compiler, hatnuise_cmake_extra_options, ...), except that this script writes to
#   hatnuise_mediatests_dir   default <hatnuise>/../builds/hatnuise-mediatests
# and needs neither Qt nor uise-desktop.

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

usage()
{
    sed -n '3,/^set -euo/p' "${BASH_SOURCE[0]}" | sed -e '/^set -euo/d' -e 's/^# \{0,1\}//'
}

# ---------------------------------------------------------------------------------------------
# Arguments

dry_run=0
if [ -n "${PRINT_ONLY:-}" ] && [ "${PRINT_ONLY}" != "0" ]
then
    dry_run=1
fi

build_arg=""
module_arg=""
jobs_arg=""
require_codec=0
do_clean=0
do_run=1

while [ $# -gt 0 ]
do
    case "$1" in
        --module)
            [ $# -ge 2 ] || die "--module needs a hatn module name"
            module_arg="$2"
            shift 2
            ;;
        --jobs)
            [ $# -ge 2 ] || die "--jobs needs a number"
            jobs_arg="$2"
            shift 2
            ;;
        --require-codec) require_codec=1; shift ;;
        --no-run) do_run=0; shift ;;
        --clean) do_clean=1; shift ;;
        --dry-run) dry_run=1; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) die "unknown option: $1 (see --help)" ;;
        *)
            [ -z "$build_arg" ] || die "unexpected argument: $1 (the build type is already '$build_arg')"
            build_arg="$1"
            shift
            ;;
    esac
done

# ---------------------------------------------------------------------------------------------
# Resolve

resolve_hatn "$build_arg" "$jobs_arg" debug

uise_src=""
module="${module_arg:-media}"

working_dir_root="${hatnuise_mediatests_dir:-$hatnuise_root/../builds/hatnuise-mediatests}"
working_dir_root="$(abs_path_new "$working_dir_root")"
configuration="$compiler-$module-$build"
build_dir="$working_dir_root/builds/$configuration"
install_prefix="$working_dir_root/install/$configuration"

# One test executable per suite, named <module><suite> in lower case, and one ctest test per suite
# under the suite's own name: hatn's ADD_HATN_CTESTS (cmake/hatn/ConfigTest.cmake) finds the suite
# with BOOST_AUTO_TEST_SUITE( or BOOST_FIXTURE_TEST_SUITE( in each test source. Doing the same here
# keeps this list from going stale when a suite is added.
media_suites=()
media_targets=()
suites_text=""
targets_text=""
if [ -d "$hatn_src/media/test" ]
then
    for test_source in "$hatn_src"/media/test/*.cpp
    do
        suite=""
        suite="$(grep -h -o -E 'BOOST_(AUTO|FIXTURE)_TEST_SUITE\( *[A-Za-z_0-9]+' "$test_source" | head -n 1 | sed -E 's/^.*\( *//')" || true
        if [ -n "$suite" ]
        then
            target="media$(echo "$suite" | tr '[:upper:]' '[:lower:]')"
            media_suites+=("$suite")
            media_targets+=("$target")
            suites_text="$suites_text $suite"
            targets_text="$targets_text $target"
        fi
    done
fi

# ---------------------------------------------------------------------------------------------
# Print

note "hatn media tests"
if [ "$dry_run" = 1 ]
then
    note "  mode                 : DRY RUN, nothing is executed"
else
    note "  mode                 : real"
fi
note "  configuration        : $configuration"
note "  hatn module          : $module (hatn is the master project, so its test/ directory is built)"
print_hatn_config
note "  working directory    : $working_dir_root"
note "  build directory      : $build_dir"
note "  test suites          :${suites_text:- (none found)}"
note "  test targets         :${targets_text:- (none found)}"

# ---------------------------------------------------------------------------------------------
# Preflight

preflight_hatn
[ -f "$hatn_src/media/CMakeLists.txt" ] || die "this hatn tree has no media module ($hatn_src/media): is hatn_src the canonical tree?"
[ -f "$hatn_src/media/test/test.cmake" ] || die "hatn media has no tests registered: $hatn_src/media/test/test.cmake"
if [ "$module" != "all" ]
then
    [ -f "$hatn_src/$module/depends.cmake" ] || die "hatn has no module '$module' (no $hatn_src/$module/depends.cmake)"
fi
[ ${#media_targets[@]} -gt 0 ] || die "found no BOOST_*_TEST_SUITE in $hatn_src/media/test/*.cpp"

guard_working_dir "$working_dir_root"
[ -d "$(dirname "$working_dir_root")" ] || die "the parent of the working directory does not exist: $(dirname "$working_dir_root") (set hatnuise_mediatests_dir)"

check_codec "$require_codec"

# ---------------------------------------------------------------------------------------------
# Build and run

export_hatn_environment

# The Boost.Test XML logger of every hatn test writes to ${working_dir}/result-xml when the
# environment variable `working_dir` is set (test/CMakeLists.txt), else to <build>/test/result-xml.
unset working_dir || true
result_xml_dir="$build_dir/test/result-xml"

# What hatn's generated run-tests.sh puts on the path so the test executables find the dependencies.
export PATH="$PATH:$deps_root/bin:$deps_root/lib"
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:$deps_root/lib"
if [ "$platform" = macos ]
then
    export DYLD_LIBRARY_PATH="${DYLD_LIBRARY_PATH:-}:$deps_root/lib"
fi

build_hatn_cmake_args
cmake_args=(
    -S "$hatn_src"
    -B "$build_dir"
    -DCMAKE_INSTALL_PREFIX="$install_prefix"
    -DDEV_MODULE="$module"
    -DBUILD_TESTS=ON
    "${hatn_cmake_args[@]}"
)

suite_regex="^($(IFS='|'; echo "${media_suites[*]}"))\$"

note ""
if [ "$do_clean" = 1 ]
then
    case "$build_dir" in
        "$working_dir_root"/builds/?*) run rm -rf "$build_dir" ;;
        *) die "refusing to delete '$build_dir'" ;;
    esac
fi
run mkdir -p "$build_dir"
run cmake "${cmake_args[@]}"
run cmake --build "$build_dir" --target "${media_targets[@]}" -j"$build_workers"
if [ "$do_run" = 1 ]
then
    run mkdir -p "$result_xml_dir"
    run ctest --test-dir "$build_dir/test" -C "$build_type" -L SUITE -R "$suite_regex" --output-on-failure
fi

note ""
if [ "$dry_run" = 1 ]
then
    note "Dry run finished, nothing was executed."
elif [ "$opus_found" = 1 ] && [ "$ogg_found" = 1 ]
then
    note "Done. Results (Boost.Test XML): $result_xml_dir"
else
    note "Done, but WITHOUT the codec: only TestPcmRing and TestWaveform tested anything (see above)."
fi
