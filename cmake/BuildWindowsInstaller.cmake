# 由 eve_windows_installer 在依赖目标构建后调用。发行文件位置以当前预设的构建目录为准。
cmake_minimum_required(VERSION 3.21)
if(NOT IS_ABSOLUTE "${build_dir}" OR NOT EXISTS "${build_dir}/cmake_install.cmake"
   OR NOT app_version MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$")
    message(FATAL_ERROR "Run the eve_windows_installer target from the configured Release preset.")
endif()

if(DEFINED ENV{EVE_ISCC} AND NOT "$ENV{EVE_ISCC}" STREQUAL "")
    set(iscc "$ENV{EVE_ISCC}")
else()
    find_program(iscc NAMES ISCC.exe
        HINTS "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6"
              "$ENV{ProgramFiles}/Inno Setup 6" "$ENV{ProgramFiles\(x86\)}/Inno Setup 6")
endif()
if(NOT iscc OR NOT EXISTS "${iscc}")
    message(FATAL_ERROR "Install Inno Setup 6.6+ or set EVE_ISCC to the full path of ISCC.exe.")
endif()

file(REAL_PATH "${build_dir}" build_root)
set(dist "${build_root}/dist")
file(MAKE_DIRECTORY "${dist}")
# 独占同一构建目录的发行文件，暂存与编译失败时保留上一份可用安装包。
file(LOCK "${build_root}/eve-installer.lock" GUARD PROCESS TIMEOUT 60)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef nonce)
set(work "${build_root}/installer-${nonce}")
if(EXISTS "${work}")
    message(FATAL_ERROR "Temporary packaging directory already exists: ${work}")
endif()
file(MAKE_DIRECTORY "${work}/payload" "${work}/output")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${build_root}" --config Release
    --component EveRuntime --prefix "${work}/payload" COMMAND_ERROR_IS_FATAL ANY)

set(name "Eve-${app_version}-windows-x64-setup")
execute_process(COMMAND "${iscc}" /Qp "/DAppVersion=${app_version}" "/DPayloadDir=${work}/payload"
    "/O${work}/output" "/F${name}" "${CMAKE_CURRENT_LIST_DIR}/../packaging/windows/Eve.iss"
    COMMAND_ERROR_IS_FATAL ANY)
set(exe "${name}.exe")
file(SHA256 "${work}/output/${exe}" digest)
file(WRITE "${work}/output/${exe}.sha256" "${digest}  ${exe}\n")
# Windows 没有同时替换两个文件的操作。先收起旧摘要，确保任一步失败都不会留下错配的校验文件。
set(checksum "${dist}/${exe}.sha256")
if(EXISTS "${checksum}")
    file(RENAME "${checksum}" "${work}/previous.sha256")
endif()
file(RENAME "${work}/output/${exe}" "${dist}/${exe}" RESULT publish_error)
if(NOT publish_error STREQUAL "0")
    if(EXISTS "${work}/previous.sha256")
        file(RENAME "${work}/previous.sha256" "${checksum}")
    endif()
    message(FATAL_ERROR "Could not publish ${exe}: ${publish_error}")
endif()
file(RENAME "${work}/output/${exe}.sha256" "${checksum}")
file(REAL_PATH "${work}" verified_work)
if(verified_work STREQUAL work AND IS_DIRECTORY "${work}")
    file(REMOVE_RECURSE "${work}")
endif()
message(STATUS "Installer: ${dist}/${exe}\nSHA-256: ${digest}")
