include_guard(GLOBAL)
# One exact-toolchain contract for production plugins and their negative fixtures.
function(cordis_qt_toolchain_json output)
    set(${output} "{\"sdkVersion\":\"${PROJECT_VERSION}\",\"qtVersion\":\"5.15.2\",\"compiler\":\"MSVC\",\"compilerVersion\":\"${CMAKE_CXX_COMPILER_VERSION}\",\"runtime\":\"$<IF:$<CONFIG:Debug>,MDd,MD>\",\"pointerBits\":64,\"cppStandard\":201703,\"iteratorDebug\":$<IF:$<CONFIG:Debug>,2,0>,\"debug\":$<IF:$<CONFIG:Debug>,true,false>}" PARENT_SCOPE)
endfunction()
