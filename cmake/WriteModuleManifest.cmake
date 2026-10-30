# Generate the digest only after linking the actual DLL, including Debug/ASan builds.
foreach(required LIBRARY MODULE_ID PLUGIN_ID)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
if(NOT DEFINED LOADER)
    set(LOADER "c-abi")
endif()
file(SHA256 "${LIBRARY}" digest)
get_filename_component(directory "${LIBRARY}" DIRECTORY)
get_filename_component(name "${LIBRARY}" NAME)
file(WRITE "${directory}/module.json"
    "{\n  \"manifestVersion\": 1,\n  \"moduleId\": \"${MODULE_ID}\",\n  \"moduleVersion\": \"1.0.0\",\n  \"loader\": \"${LOADER}\",\n  \"library\": \"${name}\",\n  \"sha256\": \"${digest}\",\n  \"platform\": \"windows-x64\",\n  \"abi\": {\"major\": 1, \"minor\": 0},\n  \"plugins\": [\"${PLUGIN_ID}\"],\n  \"unloadPolicy\": \"resident\"\n}\n")
