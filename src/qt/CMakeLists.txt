# A same-toolchain plugin must call the same scheduler implementation as its host:
# a static copy would register a different private Qt wake event type in each DLL.
set(cordis_qt_type STATIC)
if(CORDIS_SHARED_CORE)
    set(cordis_qt_type SHARED)
endif()
add_library(cordis_qt ${cordis_qt_type} scheduler.cpp object.cpp clock.cpp connection.cpp worker.cpp)
add_library(Cordis::Qt ALIAS cordis_qt)
target_link_libraries(cordis_qt PUBLIC Cordis::Core Qt5::Core)
target_compile_definitions(cordis_qt PUBLIC QT_NO_KEYWORDS)
cordis_target_defaults(cordis_qt)
if(CORDIS_SHARED_CORE)
    set_target_properties(cordis_qt PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON DEBUG_POSTFIX d)
endif()
