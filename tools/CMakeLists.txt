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

    add_dependencies(5dtools copy_pgn_fixtures)

endif()
