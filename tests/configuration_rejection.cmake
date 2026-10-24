# 负例必须以预期的 schema 诊断退出，缺文件、崩溃或其他错误不能代替校验成功。
execute_process(COMMAND "${DEMO}" "${BASE}" "${CANDIDATE}"
    RESULT_VARIABLE result ERROR_VARIABLE diagnostic OUTPUT_VARIABLE output)
if(NOT result EQUAL 1 OR NOT diagnostic MATCHES "demo.profile config must be work or personal")
    message(FATAL_ERROR "Expected schema rejection; exit=${result}; stderr=${diagnostic}; stdout=${output}")
endif()
