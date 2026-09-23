CMAKE_MINIMUM_REQUIRED (VERSION 3.16)
PROJECT(hatnuise VERSION 0.1.1 LANGUAGES CXX)

# Objective-C++ for src/microphonepermission_mac.mm (AVFoundation). Enabled here rather than in the
# LANGUAGES list of PROJECT() above, which would probe for an ObjC++ compiler on every platform.
# Same pattern as whitemdesktop/CMakeLists.txt for its own _mac.mm files.
IF (APPLE)
    ENABLE_LANGUAGE(OBJCXX)
ENDIF ()

SET(CMAKE_CXX_STANDARD_REQUIRED ON)

SET(CMAKE_AUTOMOC ON)
SET(CMAKE_AUTORCC ON)
SET(CMAKE_AUTOUIC ON)

IF ("$ENV{QT_HOME}" STREQUAL "")
ELSE()
    MESSAGE (STATUS "Using Qt from $ENV{QT_HOME}")
    SET (QT_HOME $ENV{QT_HOME})
    SET (Qt6_DIR ${QT_HOME}/lib/cmake/Qt6)
    SET (Qt6CoreTools_DIR ${QT_HOME}/lib/cmake/Qt6CoreTools)
    SET (Qt6GuiTools_DIR ${QT_HOME}/lib/cmake/Qt6GuiTools)
    SET (Qt6WidgetsTools_DIR ${QT_HOME}/lib/cmake/Qt6WidgetsTools)
ENDIF()

GET_DIRECTORY_PROPERTY(HasParent PARENT_DIRECTORY)
IF(HasParent)
    OPTION(HATN_UISE_DEMO "Build demo appliations for hatnuise lib" OFF)
ELSE()
    OPTION(HATN_UISE_DEMO "Build demo appliations for hatnuise lib" ON)
ENDIF()

FIND_PACKAGE(Qt6 COMPONENTS Core Widgets REQUIRED)

# Voice messages: glue over hatn media (hatnmedia) and Qt Multimedia. Both are optional. hatnmedia
# exists only where the parent project built the hatn media module, and Qt Multimedia only where
# uise-desktop was built with UISE_DESKTOP_MULTIMEDIA. When either is missing nothing below is
# touched: no extra find_package, no extra source, no compile definition. That is the case for a
# hatn without the media module, which this tree can be built against.
OPTION(HATN_UISE_VOICE "Build the voice message glue of hatnuise (needs hatnmedia and Qt Multimedia)" ON)
SET (HATNUISE_VOICE_ENABLED OFF)
IF (HATN_UISE_VOICE)
    IF (TARGET hatnmedia AND UISE_DESKTOP_MULTIMEDIA)
        # 6.5 is the first Qt with QMicrophonePermission, which the recorder uses
        FIND_PACKAGE(Qt6 6.5 COMPONENTS Multimedia REQUIRED)
        SET (HATNUISE_VOICE_ENABLED ON)
    ELSE ()
        MESSAGE(STATUS "hatnuise: voice message glue disabled, it needs the hatnmedia target and UISE_DESKTOP_MULTIMEDIA (currently '${UISE_DESKTOP_MULTIMEDIA}')")
    ENDIF ()
ENDIF ()

SET (HEADERS
    include/hatnuise/hatnuise.h
    include/hatnuise/string.h
    include/hatnuise/desktoperror.h
    include/hatnuise/microphonepermission.h
    include/hatnuise/uisehatnbridge.h
    include/hatnuise/uisehatneventdispatcher.h
    include/hatnuise/objectpanel.h
    include/hatnuise/datetime.h
    include/hatnuise/uid.h
)

SET (SOURCES
    src/uisehatnbridge.cpp
    src/uisehatneventdispatcher.cpp
    src/objectpanel.cpp
)

# Microphone permission. Deliberately outside the voice block below: it needs neither hatnmedia nor
# Qt Multimedia, and audio settings pages that only test a microphone need the same answer as the
# voice recorder. One implementation per platform chosen by file, as whitemdesktop chooses
# src/osnotificationsettings{_mac.mm,_win.cpp,.cpp}.
#   Apple -> AVFoundation directly; Qt's QMicrophonePermission cannot be used, see the file itself.
#   other -> QMicrophonePermission.
IF (APPLE)
    LIST (APPEND SOURCES src/microphonepermission_mac.mm)
ELSE ()
    LIST (APPEND SOURCES src/microphonepermission.cpp)
ENDIF ()

IF (HATNUISE_VOICE_ENABLED)
    LIST (APPEND HEADERS
        include/hatnuise/voiceplaybackengine.h
        include/hatnuise/voicerecordingresult.h
        include/hatnuise/voicerecorderengine.h
    )
    LIST (APPEND SOURCES
        src/voiceplaybackengine.cpp
        src/voicerecorderengine.cpp
    )
ENDIF ()

ADD_LIBRARY(${PROJECT_NAME} SHARED ${HEADERS} ${SOURCES})
TARGET_COMPILE_DEFINITIONS(${PROJECT_NAME} PRIVATE -DHATN_UISE_BUILD)

TARGET_LINK_LIBRARIES(${PROJECT_NAME} PUBLIC Qt6::Core Qt6::Widgets hatnclientserver hatnclientapp uisedesktop)
TARGET_INCLUDE_DIRECTORIES(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# AVFoundation for src/microphonepermission_mac.mm, Foundation for its NSString and blocks.
IF (APPLE)
    FIND_LIBRARY(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    FIND_LIBRARY(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    TARGET_LINK_LIBRARIES(${PROJECT_NAME} PRIVATE ${AVFOUNDATION_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
ENDIF ()

# hatnmedia is PUBLIC because the voice headers expose hatn media types to the caller; Qt Multimedia
# stays PRIVATE, no hatnuise header includes a Qt Multimedia header. HATN_UISE_VOICE is PUBLIC so a
# consumer can compile its own voice code conditionally.
IF (HATNUISE_VOICE_ENABLED)
    TARGET_LINK_LIBRARIES(${PROJECT_NAME} PUBLIC hatnmedia PRIVATE Qt6::Multimedia)
    TARGET_COMPILE_DEFINITIONS(${PROJECT_NAME} PUBLIC -DHATN_UISE_VOICE)
ENDIF ()

IF (INSTALL_DEV)
    INSTALL(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
ELSE(INSTALL_DEV)
    INSTALL(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
    )
ENDIF(INSTALL_DEV)

IF (INSTALL_DEV)
    INSTALL(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/hatnuise DESTINATION include)
ENDIF(INSTALL_DEV)

IF (HATN_UISE_DEMO)
    ADD_SUBDIRECTORY(demo)
ENDIF(HATN_UISE_DEMO)
