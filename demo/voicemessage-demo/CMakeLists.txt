CMAKE_MINIMUM_REQUIRED (VERSION 3.16)
PROJECT(voicemessage-demo VERSION 0.1.0 LANGUAGES CXX)

# Added by demo/CMakeLists.txt only when the voice glue of hatnuise is enabled, that is when it has
# hatnmedia and Qt Multimedia. Qt Multimedia is PRIVATE in hatnuise, and the demo lists the audio
# devices itself.
FIND_PACKAGE(Qt6 6.5 COMPONENTS Multimedia REQUIRED)

SET (SOURCES
    main.cpp
)

ADD_EXECUTABLE(${PROJECT_NAME} ${SOURCES})

# hatncrypt is named because the demo opens a crypt::CryptFile itself: hatnuise does not link it directly.
TARGET_LINK_LIBRARIES(${PROJECT_NAME} PUBLIC hatnuise hatncrypt Qt6::Multimedia)

# The recorder engine works with a plain file or an encrypted one. The encrypted one needs the openssl
# crypt plugin, which the demo looks for in plugins/crypt next to its executable, the folder that
# hatn's own tests use. The plugin is a dynamic library that nothing links, so it is copied here.
IF (TARGET hatnopenssl)
    ADD_DEPENDENCIES(${PROJECT_NAME} hatnopenssl)
    ADD_CUSTOM_COMMAND(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/crypt
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:hatnopenssl> $<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/crypt/
        COMMENT "Copying the openssl crypt plugin next to ${PROJECT_NAME}"
    )
ELSE ()
    MESSAGE(STATUS "voicemessage-demo: the hatnopenssl target does not exist (BUILD_PLUGINS without openssl?), the encrypted recording option will report that")
ENDIF ()

# macOS ends a process that uses the microphone without NSMicrophoneUsageDescription in its Info.plist.
# The demo is a plain executable, so the plist is embedded in a linker section, as uise-desktop does it
# for its demos (cmake/uisedemo.cmake); uise_demo() itself is only defined inside uise-desktop's own demo build.
IF (APPLE)
    SET(MACOSX_BUNDLE_BUNDLE_NAME "Voice Message Demo")
    SET(MACOSX_BUNDLE_GUI_IDENTIFIER "org.uise.hatnuise.demo.${PROJECT_NAME}")
    SET(MACOSX_BUNDLE_EXECUTABLE_NAME ${PROJECT_NAME})
    SET(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    SET(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    SET(MACOSX_BUNDLE_LONG_VERSION_STRING ${PROJECT_VERSION})
    SET(MACOSX_BUNDLE_INFO_STRING "Voice Message Demo")
    SET(MACOSX_BUNDLE_COPYRIGHT "")
    SET(MACOSX_BUNDLE_ICON_FILE "")

    SET(plistOut ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-Info.plist)
    CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in ${plistOut})
    TARGET_LINK_OPTIONS(${PROJECT_NAME} PRIVATE "LINKER:-sectcreate,__TEXT,__info_plist,${plistOut}")

    # The embedded plist alone is not enough for QMicrophonePermission. Qt's microphone permission plugin
    # is a static library that is linked into an executable only when the target has the property
    # MACOSX_BUNDLE_INFO_PLIST and that plist holds NSMicrophoneUsageDescription (checked here, at configure
    # time, by _qt_internal_darwin_permission_finalizer in Qt6CoreMacros.cmake), and Qt runs that finalizer
    # by itself only for targets made with qt_add_executable. Without it the program runs but Qt logs
    # "Could not find permission plugin for QMicrophonePermission" and the permission is never granted.
    # The property is ignored by CMake for a target that is not a bundle.
    SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${plistOut})
    QT6_FINALIZE_TARGET(${PROJECT_NAME})
ENDIF ()
