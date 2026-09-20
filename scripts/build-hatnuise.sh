#!/bin/bash

# Builds hatnuise and its demos on their own, against the canonical hatn (with its `media` module)
# and the canonical uise-desktop, without going through whitemdesktop. See scripts/README.md.
#
# Usage: build-hatnuise.sh [debug|release|minsize_release] [options]
#
#   --module <name>     hatn DEV_MODULE to build (default clientapp). `all` is the fallback if the
#                       explicit `media` step of the superbuild does not work: it builds media
#                       through hatn's own module loop, at the cost of grpcclient and its deps.
#   --no-media          do not build the hatn media module
#   --no-demo           do not build the hatnuise demos
#   --require-codec     fail unless libopus and libogg are installed in the deps root (exports
#                       HATN_USE_OPUS=1, which media/CMakeLists.txt turns into a hard error)
#   --translations      build hatn with ENABLE_TRANSLATIONS (needs gettext's msgfmt; off by default,
#                       nothing hatnuise or its demos use needs it)
#   --clean             delete the build directory first
#   --install           also run `cmake --install` into the install prefix
#   --jobs <n>          parallel jobs (default: $build_workers, else 6)
#   --dry-run           resolve and print everything, run nothing (also: PRINT_ONLY=1)
#   -h, --help          this text
#
# The build type defaults to debug. Environment (the names hatn's build scripts and
# uise-desktop/build/unix-ci.sh use):
#   hatnuise_build            debug | release | minsize_release (a positional argument wins)
#   hatnuise_compiler         clang (default) | gcc
#   hatnuise_module           same as --module
#   hatn_src                  canonical hatn tree            (default <hatnuise>/../../hatn/hatn)
#   uise_src                  canonical uise-desktop tree    (default <hatnuise>/../uise-desktop)
#   deps_universal_root       dependencies, holds root-<compiler>   (default <hatn_src>/../deps)
#   deps_root                 dependencies root itself, overrides the above
#   boost_version             picks <deps_root>/lib/cmake/Boost-<version> (default: the only one there)
#   system_boost, system_openssl   set to search the system instead of the deps root
#   hatn_plugins              hatn plugins to build (default "openssl;rocksdb")
#   QT_HOME                   Qt prefix, i.e. the directory with lib/cmake/Qt6
#   PYTHON_EXE                python for hatn's cmake (default python3)
#   build_workers             parallel jobs (default 6)
#   project_working_dir       everything is written here (default <hatnuise>/../builds/hatnuise)
#   hatnuise_install_prefix   default <project_working_dir>/install/<configuration>
#   hatnuise_cmake_extra_options   extra cmake arguments, split on spaces

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
with_media=1
with_demo=1
require_codec=0
do_clean=0
do_install=0

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
        --no-media) with_media=0; shift ;;
        --no-demo) with_demo=0; shift ;;
        --require-codec) require_codec=1; shift ;;
        --translations) translations_word=ON; shift ;;
        --clean) do_clean=1; shift ;;
        --install) do_install=1; shift ;;
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

if [ "$require_codec" = 1 ] && [ "$with_media" = 0 ]
then
    die "--require-codec and --no-media contradict each other"
fi

# ---------------------------------------------------------------------------------------------
# Resolve

superbuild_dir="$script_dir/standalone"

resolve_hatn "$build_arg" "$jobs_arg" debug

uise_src="$(abs_path "${uise_src:-$hatnuise_root/../uise-desktop}")"

module="${module_arg:-${hatnuise_module:-clientapp}}"

if [ -z "${QT_HOME:-}" ] && [ -f /opt/homebrew/lib/cmake/Qt6/Qt6Config.cmake ]
then
    QT_HOME=/opt/homebrew
fi
QT_HOME="${QT_HOME:-}"

if [ -z "${project_working_dir:-}" ]
then
    project_working_dir="$hatnuise_root/../builds/hatnuise"
fi
project_working_dir="$(abs_path_new "$project_working_dir")"

configuration="$compiler-$module-$build"
build_dir="$project_working_dir/builds/$configuration"
install_prefix="${hatnuise_install_prefix:-$project_working_dir/install/$configuration}"
demo_binary="$build_dir/hatnuise/demo/objectpanel-demo/objectpanel-demo"

media_word=OFF
if [ "$with_media" = 1 ]
then
    media_word=ON
fi
demo_word=OFF
if [ "$with_demo" = 1 ]
then
    demo_word=ON
fi

# ---------------------------------------------------------------------------------------------
# Print

note "hatnuise standalone build"
if [ "$dry_run" = 1 ]
then
    note "  mode                 : DRY RUN, nothing is executed"
else
    note "  mode                 : real"
fi
note "  configuration        : $configuration"
if [ "$with_media" = 1 ]
then
    note "  hatn module          : $module + media (added by the superbuild unless already part of $module)"
else
    note "  hatn module          : $module (without media)"
fi
note "  demos                : $demo_word"
print_hatn_config
note "  uise-desktop source  : $uise_src"
note "  hatnuise source      : $hatnuise_root"
note "  superbuild project   : $superbuild_dir"
note "  Qt                   : ${QT_HOME:-(QT_HOME is not set)}"
note "  working directory    : $project_working_dir"
note "  build directory      : $build_dir"
if [ "$do_install" = 1 ]
then
    note "  install prefix       : $install_prefix"
else
    note "  install prefix       : $install_prefix  (only used with --install)"
fi

# ---------------------------------------------------------------------------------------------
# Preflight

[ -f "$hatnuise_root/CMakeLists.txt" ] || die "not a hatnuise tree, no CMakeLists.txt in $hatnuise_root"
[ -f "$superbuild_dir/CMakeLists.txt" ] || die "the superbuild project is missing: $superbuild_dir/CMakeLists.txt"
[ -d "$uise_src" ] || die "uise-desktop source tree not found: $uise_src (set uise_src)"
[ -f "$uise_src/CMakeLists.txt" ] || die "uise_src is not a uise-desktop tree, no CMakeLists.txt in $uise_src"

preflight_hatn

if [ "$module" != "all" ]
then
    [ -f "$hatn_src/$module/depends.cmake" ] || die "hatn has no module '$module' (no $hatn_src/$module/depends.cmake)"
fi
if [ "$with_media" = 1 ]
then
    [ -f "$hatn_src/media/CMakeLists.txt" ] || die "this hatn tree has no media module ($hatn_src/media): is hatn_src the canonical tree? (or use --no-media)"
fi

guard_working_dir "$project_working_dir"
[ -d "$(dirname "$project_working_dir")" ] || die "the parent of project_working_dir does not exist: $(dirname "$project_working_dir") (set project_working_dir)"

[ -n "$QT_HOME" ] || die "QT_HOME is not set: give the Qt prefix, the directory that has lib/cmake/Qt6"
[ -f "$QT_HOME/lib/cmake/Qt6/Qt6Config.cmake" ] || die "no Qt 6 at QT_HOME=$QT_HOME (missing lib/cmake/Qt6/Qt6Config.cmake). QT_HOME is the prefix, not the cmake directory"
for qt_package in Qt6Svg Qt6LinguistTools
do
    [ -d "$QT_HOME/lib/cmake/$qt_package" ] || die "Qt at $QT_HOME has no $qt_package (uise-desktop needs it)"
done
case "$hatnuise_cmake_extra_options" in
    *UISE_DESKTOP_MULTIMEDIA=OFF*) ;;
    *)
        for qt_package in Qt6Multimedia Qt6MultimediaWidgets
        do
            [ -d "$QT_HOME/lib/cmake/$qt_package" ] || die "Qt at $QT_HOME has no $qt_package (uise-desktop needs it unless -DUISE_DESKTOP_MULTIMEDIA=OFF)"
        done
        ;;
esac
export QT_HOME

if [ "$with_media" = 1 ]
then
    check_codec "$require_codec"
fi

# ---------------------------------------------------------------------------------------------
# Build

export_hatn_environment
export PATH="$QT_HOME/bin:$PATH"

build_hatn_cmake_args
cmake_args=(
    -S "$superbuild_dir"
    -B "$build_dir"
    -DCMAKE_INSTALL_PREFIX="$install_prefix"
    -DDEV_MODULE="$module"
    -DHATN_UISE_DEMO="$demo_word"
    -DHATNUISE_WITH_MEDIA="$media_word"
    -DHATNUISE_HATN_SRC="$hatn_src"
    -DHATNUISE_UISE_SRC="$uise_src"
    -DHATNUISE_SRC="$hatnuise_root"
    "${hatn_cmake_args[@]}"
)

note ""
if [ "$do_clean" = 1 ]
then
    # build_dir is <project_working_dir>/builds/<configuration>, which guard_working_dir has shown to
    # be outside every source tree.
    case "$build_dir" in
        "$project_working_dir"/builds/?*) run rm -rf "$build_dir" ;;
        *) die "refusing to delete '$build_dir'" ;;
    esac
fi
run mkdir -p "$build_dir"
run cmake "${cmake_args[@]}"
run cmake --build "$build_dir" -j"$build_workers"
if [ "$do_install" = 1 ]
then
    run cmake --install "$build_dir" --prefix "$install_prefix"
fi

note ""
if [ "$dry_run" = 1 ]
then
    note "Dry run finished, nothing was executed."
else
    note "Done."
    if [ "$with_demo" = 1 ]
    then
        if [ -x "$demo_binary" ]
        then
            note "  objectpanel-demo: $demo_binary"
        else
            note "  expected the demo at $demo_binary but it is not there"
        fi
    fi
fi
