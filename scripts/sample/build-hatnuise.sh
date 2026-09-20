#!/bin/bash

set -e

# Standalone hatnuise + demos against the CANONICAL hatn and uise-desktop trees.
# Usage: ./build-hatnuise.sh [debug|release] [--dry-run] [--clean] [--require-codec] ...
#        (see hatnuise/scripts/build-hatnuise.sh --help)
# Nothing is written outside project_working_dir: not the whitemdesktop build, not its deps.

if [ -z "$hatnuise_build" ];
then
export hatnuise_build=debug
fi
export build_workers=14
export QT_HOME=/opt/homebrew
export deps_universal_root=$HOME/projects/hatn/deps
export boost_version=1.87.0
export hatn_src=$HOME/projects/hatn/hatn
export uise_src=$HOME/projects/uise/uise-desktop
export project_working_dir=$HOME/projects/uise/builds/hatnuise
export PYTHON_EXE=python3
#export hatnuise_cmake_extra_options="-DUISE_DESKTOP_MULTIMEDIA=OFF"

$HOME/projects/uise/hatnuise/scripts/build-hatnuise.sh "$@"
