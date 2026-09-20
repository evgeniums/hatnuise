# hatnuise build scripts

hatnuise has no CMake project of its own that can stand alone: its `CMakeLists.txt` links the bare
targets `hatnclientserver`, `hatnclientapp` and `uisedesktop` and expects a parent project to define them.
Inside whitemdesktop the parent is whitemdesktop. These scripts are the other parent: they build hatnuise
and its demos against the canonical hatn and uise-desktop trees, in a working directory of their own.
The whitemdesktop build never enters this directory.

| File | Role |
|---|---|
| `standalone/CMakeLists.txt` | The superbuild: adds hatn, hatn `media`, uise-desktop and hatnuise, in that order |
| `build-hatnuise.sh` | Resolves paths, checks the environment, configures and builds (incremental) |
| `build-media-tests.sh` | Optional: builds and runs hatn's `media` tests through hatn's own CMake and ctest |
| `common.sh` | Shared by the two (sourced) |

The entry point you normally run is outside the repository, next to `build-uise.sh`:

```bash
cd ~/projects/uise
./build-hatnuise.sh              # debug
./build-hatnuise.sh release
./build-hatnuise.sh --dry-run    # resolve and print everything, run nothing
./build-hatnuise.sh --help
```

It exports the environment (`QT_HOME`, `deps_universal_root`, `boost_version`, `build_workers`, ...) and
hands over to `build-hatnuise.sh`. Everything is written under
`~/projects/uise/builds/hatnuise/{builds,install}/<compiler>-<module>-<debug|release>`; the demo ends up in
`builds/<configuration>/hatnuise/demo/objectpanel-demo/objectpanel-demo`.

## The Ogg/Opus codec

hatn `media` uses libopus and libogg when they are in the dependencies root and builds without them
otherwise (`media/CMakeLists.txt`: two `FIND_PACKAGE(... CONFIG)`, results written into a generated
`config.h`). `build-hatnuise.sh` looks for the same config packages and, when they are missing, says so and
prints the exact command that builds them. `--require-codec` makes their absence an error. After building
them just run the script again, no `--clean` is needed.

## Media tests

```bash
hatnuise/scripts/build-media-tests.sh [debug|release] [--require-codec] [--no-run] [--clean]
```

hatn builds its tests only when it is the master project, so this configures hatn itself (its own tree,
its own `test/`), builds media's six test executables and runs them with hatn's ctest labels. Working
directory: `~/projects/uise/builds/hatnuise-mediatests`. It does not use hatn's `build` driver, which wipes
its build directory on every run and cannot limit the build to media's test targets. The default module is
`media` (media plus its five dependencies); `--module all` builds every hatn module instead.

`--module mediatests` runs the voice message tests over a real `crypt::CryptFile` (record, play, seek,
crop, and the pause that closes the file and reopens it in append mode). `mediatests` is a module of
tests only, in hatn, so that media itself does not depend on crypt. Its tests load the openssl crypto
plugin, so `hatn_plugins` defaults to `openssl` for it; a run needs the codec as well to exercise
anything but the append-mode cases.

After ctest the script prints, per test case, its time and every `BOOST_TEST_MESSAGE`, warning and error,
read from `<build>/test/result-xml/*.xml`: hatn's ctest registration logs only test suites to the console, so a
passing run otherwise shows nothing but its status. `--quiet` turns that off.

## Fallback if the superbuild's `media` step fails

`DEV_MODULE=clientapp` does not include `media`, so the superbuild adds it itself with
`ADD_SUBDIRECTORY(${HATN_MEDIA_SRC} ...)`. If that does not configure, `build-hatnuise.sh --module all`
lets hatn's own loop add it, at the cost of also building `grpcclient`.
