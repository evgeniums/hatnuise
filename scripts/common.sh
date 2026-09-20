#!/bin/bash

# Shared by build-hatnuise.sh and build-media-tests.sh. Sourced, never executed.
#
# The caller has already done `set -euo pipefail` and defines `dry_run` (0 or 1) before it calls
# run(). Everything here follows the same three steps: resolve (compute values, never fail on a
# missing directory), print, then preflight (fail early, name what is missing). Functions end in an
# `if` or `return 0` on purpose: under `set -e` a trailing `[ ... ] && ...` would make the caller exit.
#
# Variable names are the ones hatn's build scripts (build/lib/unix/cfg.sh, run.sh) and
# uise-desktop/build/unix-ci.sh already use.

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
hatnuise_root="$(cd "$script_dir/.." && pwd)"

die()
{
    echo "ERROR: $*" >&2
    exit 1
}

note()
{
    echo "$*"
}

# An existing directory as an absolute path, anything else unchanged (it is reported by preflight).
abs_path()
{
    if [ -d "$1" ]
    then
        (cd "$1" && pwd)
    else
        echo "$1"
    fi
}

# Same for a directory that is normally created by the build: normalise through its parent.
abs_path_new()
{
    local parent
    parent="$(dirname "$1")"
    if [ -d "$parent" ]
    then
        echo "$(cd "$parent" && pwd)/$(basename "$1")"
    else
        echo "$1"
    fi
}

# Prints the command; runs it unless this is a dry run.
run()
{
    printf '+'
    printf ' %q' "$@"
    printf '\n'
    if [ "$dry_run" = 0 ]
    then
        "$@"
    fi
}

# ---------------------------------------------------------------------------------------------
# hatn: locations, compiler, configuration
#
# resolve_hatn <build type or ""> <jobs or ""> <default build type>

resolve_hatn()
{
    hatn_src="$(abs_path "${hatn_src:-$hatnuise_root/../../hatn/hatn}")"

    compiler="${hatnuise_compiler:-clang}"
    case "$compiler" in
        clang) cc_name=clang; cxx_name=clang++ ;;
        gcc) cc_name=gcc; cxx_name=g++ ;;
        *) die "hatnuise_compiler must be clang or gcc, not '$compiler'" ;;
    esac

    build="${1:-${hatnuise_build:-$3}}"
    case "$build" in
        release) build_type=Release ;;
        debug) build_type=Debug ;;
        minsize_release) build_type=MinSizeRel ;;
        *) die "build must be debug, release or minsize_release, not '$build'" ;;
    esac

    build_workers="${2:-${build_workers:-6}}"
    case "$build_workers" in
        ''|*[!0-9]*) die "the number of jobs must be a positive integer, not '$build_workers'" ;;
    esac

    hatn_plugins="${hatn_plugins:-openssl;rocksdb}"
    hatnuise_cmake_extra_options="${hatnuise_cmake_extra_options:-}"
    translations_word="${translations_word:-OFF}"

    case "$(uname)" in
        Darwin) platform=macos ;;
        Linux) platform=linux ;;
        *) die "unsupported platform '$(uname)': only macOS and Linux are wired up" ;;
    esac

    deps_universal_root="${deps_universal_root:-$hatn_src/../deps}"
    if [ -z "${deps_root:-}" ]
    then
        deps_root="$deps_universal_root/root-$compiler"
    fi
    deps_root="$(abs_path "$deps_root")"

    PYTHON_EXE="${PYTHON_EXE:-python3}"

    # Boost: <deps_root>/lib/cmake/Boost-<version>, the way hatn's run.sh forms it. Without
    # boost_version the only Boost-* directory there is used.
    boost_arg=""
    boost_dir=""
    if [ -z "${system_boost:-}" ]
    then
        if [ -z "${boost_version:-}" ]
        then
            local boost_dirs
            boost_dirs=("$deps_root"/lib/cmake/Boost-*)
            if [ ${#boost_dirs[@]} -eq 1 ] && [ -d "${boost_dirs[0]}" ]
            then
                boost_version="${boost_dirs[0]##*/Boost-}"
            fi
        fi
        if [ -n "${boost_version:-}" ]
        then
            boost_dir="$deps_root/lib/cmake/Boost-$boost_version"
            boost_arg="-DBoost_DIR=$boost_dir"
        fi
    fi
    return 0
}

# Refuses a working directory that lies inside one of the source trees.
guard_working_dir()
{
    local dir="$1"
    local src
    for src in "$hatn_src" "$hatnuise_root" "${uise_src:-}"
    do
        if [ -n "$src" ]
        then
            case "$dir/" in
                "$src"/*) die "the working directory ($dir) is inside the source tree $src" ;;
            esac
        fi
    done
    return 0
}

print_hatn_config()
{
    note "  platform / compiler  : $platform / $compiler ($cc_name, $cxx_name)"
    note "  build type           : $build ($build_type)"
    note "  hatn plugins         : $hatn_plugins"
    note "  hatn translations    : $translations_word"
    note "  hatn source          : $hatn_src"
    note "  dependencies root    : $deps_root"
    if [ -n "${system_boost:-}" ]
    then
        note "  boost                : system (system_boost is set)"
    elif [ -n "$boost_dir" ]
    then
        note "  boost                : $boost_dir"
    else
        note "  boost                : (boost_version is not set and the choice is not unique)"
    fi
    note "  python               : $PYTHON_EXE"
    note "  parallel jobs        : $build_workers"
}

preflight_hatn()
{
    [ -d "$hatn_src" ] || die "hatn source tree not found: $hatn_src (set hatn_src)"
    [ -f "$hatn_src/CMakeLists.txt" ] || die "hatn_src is not a hatn tree, no CMakeLists.txt in $hatn_src"
    [ -f "$hatn_src/hatn.src" ] || die "hatn_src is not a hatn tree, no hatn.src marker in $hatn_src"
    [ -d "$deps_root" ] || die "dependencies root not found: $deps_root (set deps_universal_root or deps_root)"

    command -v cmake > /dev/null 2>&1 || die "cmake is not on PATH"
    command -v make > /dev/null 2>&1 || die "make is not on PATH (the Unix Makefiles generator needs it)"
    command -v "$cc_name" > /dev/null 2>&1 || die "$cc_name is not on PATH"
    command -v "$cxx_name" > /dev/null 2>&1 || die "$cxx_name is not on PATH"
    command -v "$PYTHON_EXE" > /dev/null 2>&1 || die "python not found: '$PYTHON_EXE' (set PYTHON_EXE, hatn's cmake needs it)"
    if [ "$translations_word" = ON ]
    then
        command -v msgfmt > /dev/null 2>&1 || die "--translations needs gettext's msgfmt on PATH"
    fi

    if [ -z "${system_boost:-}" ]
    then
        [ -n "$boost_dir" ] || die "boost_version is not set and $deps_root/lib/cmake does not hold exactly one Boost-<version> directory"
        [ -d "$boost_dir" ] || die "Boost is not in the dependencies root: $boost_dir (boost_version=$boost_version)"
    fi
    if [ -z "${system_openssl:-}" ]
    then
        if [[ ";$hatn_plugins;" == *";openssl;"* ]]
        then
            [ -d "$deps_root/include/openssl" ] || die "OpenSSL is not in the dependencies root: $deps_root/include/openssl (hatn plugin 'openssl')"
        fi
    fi
    if [[ ";$hatn_plugins;" == *";rocksdb;"* ]]
    then
        [ -d "$deps_root/lib/cmake/rocksdb" ] || die "RocksDB is not in the dependencies root: $deps_root/lib/cmake/rocksdb (hatn plugin 'rocksdb')"
    fi
    return 0
}

# ---------------------------------------------------------------------------------------------
# Codec: libopus and libogg are optional for hatn media. Whether they are usable is decided by
# media/CMakeLists.txt (FIND_PACKAGE(Opus CONFIG) and Ogg, off CMAKE_PREFIX_PATH = the deps root);
# this only looks for the same config packages so it can say so before a long build.
#
# check_codec <require: 0|1>

have_config()
{
    compgen -G "$1/*onfig.cmake" > /dev/null
}

check_codec()
{
    local require="$1"
    opus_found=0
    ogg_found=0
    if have_config "$deps_root/lib/cmake/Opus"
    then
        opus_found=1
    fi
    if have_config "$deps_root/lib/cmake/Ogg"
    then
        ogg_found=1
    fi

    if [ "$opus_found" = 1 ] && [ "$ogg_found" = 1 ]
    then
        note "  Ogg/Opus codec       : libopus and libogg found in the dependencies root"
        note "                         (media's own cmake output, 'libopus and libogg found', is what counts)"
    else
        # hatn's dependency runner builds into <cwd>/deps/root-<toolchain>, so it has to be started
        # from the directory that holds ./deps. Its `arch` defaults to x86_64 when unset.
        local deps_script="$hatn_src/build/deps/desktop/build-$platform-${compiler}64.sh"
        local deps_parent
        deps_parent="$(dirname "$deps_root")"
        case "$(basename "$deps_root")" in
            root-*) deps_parent="$(dirname "$deps_parent")" ;;
        esac
        note ""
        note "libopus / libogg are NOT built in $deps_root"
        note "  (Opus: $([ "$opus_found" = 1 ] && echo found || echo missing), Ogg: $([ "$ogg_found" = 1 ] && echo found || echo missing))"
        note "  hatn media will build WITHOUT the Ogg/Opus voice codec: recording and playback will not work."
        note "  To build them, run exactly this:"
        note "      cd \"$deps_parent\" && arch=\"$(uname -m)\" dep_libs=\"ogg opus\" \"$deps_script\""
        if [ -x "$deps_parent/deps-macos-audio.sh" ]
        then
            note "  or the wrapper that is already there:  cd \"$deps_parent\" && ./deps-macos-audio.sh"
        fi
        note "  Then run this script again (no --clean needed: the codec is looked up on every configure)."
        note ""
        if [ "$require" = 1 ]
        then
            die "--require-codec: build libopus and libogg first (see above)"
        fi
    fi
    if [ "$require" = 1 ]
    then
        # media/CMakeLists.txt turns "not found" into FATAL_ERROR when this is set.
        export HATN_USE_OPUS=1
    fi
    return 0
}

# ---------------------------------------------------------------------------------------------
# What hatn's cmake reads from the environment, as hatn's own build scripts export it, and the
# arguments both drivers give cmake (the ones hatn's build/lib/unix/lib/platforms/run.sh passes).

export_hatn_environment()
{
    export CC="$cc_name"
    export CXX="$cxx_name"
    export PYTHON_EXE
    export deps_root
    if [ -z "${system_openssl:-}" ]
    then
        export OPENSSL_ROOT_DIR="$deps_root"
    fi
    return 0
}

build_hatn_cmake_args()
{
    hatn_cmake_args=(
        -G "Unix Makefiles"
        -DCMAKE_BUILD_TYPE="$build_type"
        -DCMAKE_PREFIX_PATH="$deps_root"
        -DDEPS_ROOT="$deps_root"
        -DINSTALL_DEV=1
        -DBUILD_STATIC=0
        -DENABLE_TRANSLATIONS="$translations_word"
        -DBUILD_PLUGINS="$hatn_plugins"
    )
    if [ -n "$boost_arg" ]
    then
        hatn_cmake_args+=("$boost_arg")
    fi
    if [ -n "${hatnuise_cmake_extra_options//[[:space:]]/}" ]
    then
        local extra_args
        read -r -a extra_args <<< "$hatnuise_cmake_extra_options"
        hatn_cmake_args+=("${extra_args[@]}")
    fi
    return 0
}
