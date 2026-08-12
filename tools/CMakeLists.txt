if(ENGINE)
    message(STATUS ">>> Building engine...")
    add_executable(5dchess 5dchess.cpp)
    target_link_libraries(5dchess PRIVATE 5dchess_engine)
    set_target_properties(5dchess PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()

if(TOOLS)
    message(STATUS ">>> Building tools...")
    # Every implementation file in this directory belongs to 5dtools except
    # the independent 5dchess entry point.
    file(GLOB TOOLS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
    list(REMOVE_ITEM TOOLS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/5dchess.cpp")

    add_executable(5dtools ${TOOLS_SOURCES})
    target_link_libraries(5dtools PRIVATE 5dchess_client 5dchess_engine)
    set_target_properties(5dtools PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    add_custom_command(TARGET 5dtools POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/test/pgn
        $<TARGET_FILE_DIR:5dtools>/pgn
    )

    if(TEST)
        message(STATUS "Registering tools related tests...")
        file(GLOB PGN_FILES "${CMAKE_SOURCE_DIR}/test/pgn/*.5dpgn")
        foreach(PGN_FILE ${PGN_FILES})
            get_filename_component(PGN_NAME ${PGN_FILE} NAME_WE)
            add_test(
                NAME perftest_${PGN_NAME}
                COMMAND bash -c "cat ${PGN_FILE} | $<TARGET_FILE:5dtools> perftest"
            )
        endforeach()
    endif()
endif()
