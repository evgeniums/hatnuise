ADD_SUBDIRECTORY(objectpanel-demo)

# The voice glue exists only with hatnmedia and Qt Multimedia (see the HATNUISE_VOICE_ENABLED block of the
# top CMakeLists.txt, which sets the variable before it adds this directory).
IF (HATNUISE_VOICE_ENABLED)
    ADD_SUBDIRECTORY(voicemessage-demo)
ELSE ()
    MESSAGE(STATUS "hatnuise: voicemessage-demo skipped, the voice glue is not enabled")
ENDIF ()
