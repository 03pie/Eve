# A same-toolchain plugin must call the same scheduler implementation as its host:
# a static copy would register a different private Qt wake event type in each DLL.
set(eve_qt_type STATIC)
if(EVE_SHARED_CORE)
    set(eve_qt_type SHARED)
endif()
add_library(eve_qt ${eve_qt_type} scheduler.cpp object.cpp clock.cpp connection.cpp worker.cpp)
add_library(Eve::Qt ALIAS eve_qt)
target_link_libraries(eve_qt PUBLIC Eve::Core Qt5::Core)
target_compile_definitions(eve_qt PUBLIC QT_NO_KEYWORDS)
eve_target_defaults(eve_qt)
if(EVE_SHARED_CORE)
    set_target_properties(eve_qt PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON DEBUG_POSTFIX d)
endif()
