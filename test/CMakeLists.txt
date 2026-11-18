cmake_minimum_required(VERSION 3.20)

# Find all test files in the test directory
file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

foreach(TEST_FILE ${TEST_SOURCES})
    # Extract test name without extension
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)

    # Create an executable for each test file
    add_executable(${TEST_NAME} ${TEST_FILE})
    add_dependencies(${TEST_NAME} copy_pgn_fixtures)
    # Disable LTO/IPO for this test target
    set_target_properties(${TEST_NAME} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION FALSE
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<0:>"
    )

    # Link against the core engine library
    target_link_libraries(${TEST_NAME} PRIVATE 5dchess_client 5dchess_engine)

    # Include necessary directories
    target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

    # Register test with ctest
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# Includes the formerly unbounded slow-position legality regression.
set_tests_properties(test_hcutils PROPERTIES TIMEOUT 30)

if(TOOLS)
    # Reuse the shell script's literal stdin records without requiring a shell in CTest.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/hc_count.sh" HC_COUNT_SCRIPT)
    string(REGEX MATCH "HC_PROFILE_INPUT'\n(.*)\nHC_PROFILE_INPUT" HC_COUNT_MATCH "${HC_COUNT_SCRIPT}")
    if(NOT HC_COUNT_MATCH)
        message(FATAL_ERROR "Missing hc_count.sh input records")
    endif()
    set(HC_COUNT_INPUT "${CMAKE_CURRENT_BINARY_DIR}/hc_count.in")
    file(WRITE "${HC_COUNT_INPUT}" "${CMAKE_MATCH_1}\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS hc_count.sh)
    foreach(POLICY balanced stable deferred fine)
        add_test(NAME hc_count_${POLICY}
            COMMAND ${CMAKE_COMMAND}
                "-DTOOLS=$<TARGET_FILE:5dtools>"
                "-DINPUT=${HC_COUNT_INPUT}" "-DPOLICY=${POLICY}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/run_hc_count.cmake")
        set_tests_properties(hc_count_${POLICY} PROPERTIES TIMEOUT 30)
    endforeach()
endif()
