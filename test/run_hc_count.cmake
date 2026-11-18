execute_process(
    COMMAND "${TOOLS}" hc-profile --policy "${POLICY}" --warmup-rounds 0 --rounds 1
    INPUT_FILE "${INPUT}"
    RESULT_VARIABLE STATUS
    OUTPUT_VARIABLE OUTPUT
    ERROR_VARIABLE ERRORS
)
if(NOT STATUS STREQUAL "0")
    message(FATAL_ERROR "hc-profile failed (${STATUS}):\n${OUTPUT}\n${ERRORS}")
endif()
