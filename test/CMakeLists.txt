cmake_minimum_required(VERSION 3.20)

# Find all test files in the test directory
file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

foreach(TEST_FILE ${TEST_SOURCES})
    # Extract test name without extension
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)

    # Create an executable for each test file
    add_executable(${TEST_NAME} ${TEST_FILE})

    # Link against the core engine library
    target_link_libraries(${TEST_NAME} PRIVATE 5dchess_engine_core)

    # Include necessary directories
    target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

    # Register test with ctest
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()
