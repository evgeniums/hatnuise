# Standalone superbuild for hatnuise.
#
# hatnuise's own CMakeLists.txt links the bare targets hatnclientserver, hatnclientapp and
# uisedesktop and expects a parent project to have defined them (neither hatn nor uise-desktop
# installs a find_package()-able config package). Inside whitemdesktop that parent is
# whitemdesktop/CMakeLists.txt; this file is the equivalent for building hatnuise on its own
# against the canonical hatn and uise-desktop trees. It is never entered by the whitemdesktop
# build, and hatnuise's own CMakeLists.txt is not changed by it.
#
# Driven by ../build-hatnuise.sh, which passes every HATNUISE_* variable below plus the usual hatn
# ones (CMAKE_BUILD_TYPE, DEV_MODULE, BUILD_PLUGINS, Boost_DIR, CMAKE_PREFIX_PATH, DEPS_ROOT, ...).
CMAKE_MINIMUM_REQUIRED (VERSION 3.16)
PROJECT(hatnuise-standalone LANGUAGES C CXX)

SET(HATNUISE_HATN_SRC "" CACHE PATH "Canonical hatn source root (the directory with hatn.src)")
SET(HATNUISE_UISE_SRC "" CACHE PATH "Canonical uise-desktop source root")
SET(HATNUISE_SRC "" CACHE PATH "hatnuise source root")

OPTION(HATNUISE_WITH_MEDIA "Also build the hatn media module (hatnmedia), which DEV_MODULE=clientapp does not pull in" ON)
OPTION(UISE_DESKTOP_MULTIMEDIA "Enable multimedia classes in uise-desktop" ON)

FOREACH(var HATNUISE_HATN_SRC HATNUISE_UISE_SRC HATNUISE_SRC)
    IF ("${${var}}" STREQUAL "")
        MESSAGE(FATAL_ERROR "${var} is not set. Run this project through scripts/build-hatnuise.sh.")
    ENDIF()
    IF (NOT EXISTS "${${var}}/CMakeLists.txt")
        MESSAGE(FATAL_ERROR "${var}=${${var}} has no CMakeLists.txt")
    ENDIF()
ENDFOREACH()

IF ("${CMAKE_BUILD_TYPE}" STREQUAL "")
    MESSAGE(FATAL_ERROR "CMAKE_BUILD_TYPE is not set (hatn/Config.cmake needs it). Pass Debug or Release.")
ENDIF()

# Qt: the same QT_HOME convention hatnuise and uise-desktop use in their own CMakeLists.txt, done
# once here so all three agree. Qt is also found at this scope, as whitemdesktop does, so that the
# imported targets are visible to hatnuise even though uise-desktop is a sibling directory.
IF (NOT "$ENV{QT_HOME}" STREQUAL "")
    MESSAGE (STATUS "Using Qt from $ENV{QT_HOME}")
    SET (QT_HOME $ENV{QT_HOME})
    SET (Qt6_DIR ${QT_HOME}/lib/cmake/Qt6)
    SET (Qt6CoreTools_DIR ${QT_HOME}/lib/cmake/Qt6CoreTools)
    SET (Qt6GuiTools_DIR ${QT_HOME}/lib/cmake/Qt6GuiTools)
    SET (Qt6WidgetsTools_DIR ${QT_HOME}/lib/cmake/Qt6WidgetsTools)
    SET (Qt6LinguistTools_DIR ${QT_HOME}/lib/cmake/Qt6LinguistTools)
ENDIF()
FIND_PACKAGE(Qt6 COMPONENTS Core Widgets Svg LinguistTools REQUIRED)
IF (UISE_DESKTOP_MULTIMEDIA)
    FIND_PACKAGE(Qt6 COMPONENTS Multimedia MultimediaWidgets REQUIRED)
ENDIF()

# hatn: the same three steps whitemdesktop/CMakeLists.txt performs before it adds hatn. Config.cmake
# opens with INCLUDE_GUARD(), so it runs once, here; hatn's own INCLUDE(hatn/Config) is then a no-op
# and hatn, the media module below and uise-desktop all inherit what it sets (CMAKE_CXX_STANDARD,
# HATN_COMPILE_*, HATN_INCLUDE_DIRECTORIES, LINK_TYPE, Boost, ...). This project is not a hatn
# master project, so hatn builds no tests here (see build-media-tests.sh for those).
LIST(APPEND CMAKE_MODULE_PATH "${HATNUISE_HATN_SRC}/cmake")
INCLUDE(hatn/Config)

# Order is load-bearing: hatnuise links targets from all three.
ADD_SUBDIRECTORY(${HATNUISE_HATN_SRC} ${CMAKE_BINARY_DIR}/hatn)

# DEV_MODULE takes ONE module name and hatn adds that module plus its depends.cmake closure, so
# DEV_MODULE=clientapp gives hatnuise everything it links but not media. INIT_HATN_SRC_PATHS
# (Module.cmake) sets HATN_<MODULE>_SRC as CACHE variables for the WHOLE module list before that
# narrowing, so HATN_MEDIA_SRC is available here. hatn's own ADD_HATN_SUBDIRS() cannot be reused: it
# calls ADD_SUBDIRECTORY without a binary directory, which CMake refuses for a source outside the
# current tree. The binary directory below is the one hatn itself would have used.
# BUILD_HATN_MODULE() (Build.cmake) is self-contained: it reads media/depends.cmake and links the
# hatn targets that already exist above.
IF (HATNUISE_WITH_MEDIA)
    IF (TARGET hatnmedia)
        MESSAGE(STATUS "hatnmedia is already part of the hatn build (DEV_MODULE=${DEV_MODULE})")
    ELSE()
        IF ("${HATN_MEDIA_SRC}" STREQUAL "" OR NOT EXISTS "${HATN_MEDIA_SRC}/CMakeLists.txt")
            MESSAGE(FATAL_ERROR "HATN_MEDIA_SRC='${HATN_MEDIA_SRC}' is not usable: hatn did not register the media module. "
                "Is media in ${HATNUISE_HATN_SRC}/cmake/hatn/Libs.cmake? Otherwise configure with -DHATNUISE_WITH_MEDIA=OFF.")
        ENDIF()
        MESSAGE(STATUS "Adding hatn module media from ${HATN_MEDIA_SRC}")
        ADD_SUBDIRECTORY(${HATN_MEDIA_SRC} ${HATN_BINARY_DIR}/media)
    ENDIF()
ENDIF()

ADD_SUBDIRECTORY(${HATNUISE_UISE_SRC} ${CMAKE_BINARY_DIR}/uise-desktop)
ADD_SUBDIRECTORY(${HATNUISE_SRC} ${CMAKE_BINARY_DIR}/hatnuise)
