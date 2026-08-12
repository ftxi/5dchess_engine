cmake_minimum_required(VERSION 3.20)
project(5dchess_engine VERSION 0.0.0 LANGUAGES CXX)

# Enable LTO for release builds when supported by the toolchain
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
if(IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    message(STATUS "LTO supported")
else()
    message(STATUS "LTO not supported: ${IPO_ERROR}")
endif()

# Extract version from git tags with fallback to CMake project version
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # Only use git version if we're in a git repo and got a valid tag
    if(GIT_VERSION AND NOT GIT_VERSION STREQUAL "")
        # Remove 'v' prefix if present
        string(REGEX REPLACE "^v" "" GIT_VERSION "${GIT_VERSION}")
        set(PROJECT_VERSION "${GIT_VERSION}")
        message(STATUS "Version from git tag: ${PROJECT_VERSION}")
    else()
        message(STATUS "Not in a git repo or no tags found, using default version: ${PROJECT_VERSION}")
    endif()
else()
    message(STATUS "Git not found, using default version: ${PROJECT_VERSION}")
endif()

# Make all targets PIC by default (needed for Python .so on github actions)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Print compiler information
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C compiler ID: ${CMAKE_C_COMPILER_ID}")
message(STATUS "C++ compiler ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C compiler version: ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "C++ compiler version: ${CMAKE_CXX_COMPILER_VERSION}")

# For Emscripten specifically
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER MATCHES "em\\+\\+")
    message(STATUS "Using Emscripten compiler")
endif()

# compiler-specific options for magic.cpp
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -pedantic -Wextra -Wpedantic)
    add_compile_options(-fconstexpr-steps=100000000)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wall -pedantic -Wextra -Wpedantic)
    # GCC equivalent settings
    add_compile_options(
        -fconstexpr-depth=1000000
        -fconstexpr-ops-limit=100000000
    )
elseif(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_options(/constexpr:steps100000000)
endif()

# Optional products. With every option disabled, only the core library is built.
option(ENGINE "Build the 5dchess UCI executable" OFF)
option(TOOLS "Build the 5dtools command-line utilities" OFF)
option(TEST "Build test executables" OFF)
option(PYMODULE "Build Python module" OFF)
option(EMMODULE "Build Emscripten/WebAssembly module" OFF)

# Enable testing if TEST option is ON
if(TEST)
    enable_testing()
endif()

# Find the sources for each library component.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/misc/*.cpp"
)
file(GLOB_RECURSE CLIENT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/*.cpp"
)
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.cpp"
)

# Find all header files
file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/misc/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/misc/*.inl"
)

# The client and engine components are excluded from the default build. They are
# built automatically when a requested product links against them.
add_library(5dchess_core STATIC ${CORE_SOURCES})
add_library(5dchess_client STATIC EXCLUDE_FROM_ALL ${CLIENT_SOURCES})
add_library(5dchess_engine STATIC EXCLUDE_FROM_ALL ${ENGINE_SOURCES})

target_link_libraries(5dchess_client PUBLIC 5dchess_core)
target_link_libraries(5dchess_engine PUBLIC 5dchess_core)

# Add version as compile definition for all targets using this library
target_compile_definitions(5dchess_core PUBLIC
    PROJECT_VERSION_STRING="${PROJECT_VERSION}"
)

# Ensure header files show up in IDEs
target_sources(5dchess_core PRIVATE ${ENGINE_HEADERS})

target_include_directories(5dchess_core
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/client
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/src/misc
)

target_include_directories(5dchess_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/client)
target_include_directories(5dchess_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/engine)

# Check if this is being built as a submodule
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    # Standalone mode - build only the explicitly requested products.
    if(ENGINE OR TOOLS)
        add_subdirectory(tools)
    endif()

    if(TEST)
        message(STATUS ">>> Building tests...")
        # Scratch executable for temporary, ad hoc experiments. Keep this out
        # of CTest because test.cpp is intentionally edited and cleared often.
        add_executable(test_exec test.cpp)
        target_link_libraries(test_exec PRIVATE 5dchess_client 5dchess_engine)
        add_subdirectory(test)
    endif()
    
    if(PYMODULE)
        message(STATUS ">>> Building python library...")
        # Add pybind11 for Python bindings
        set(PYBIND11_FINDPYTHON ON CACHE BOOL "Use modern FindPython mode")
        add_subdirectory(extern/pybind11)
        
        # Create the Python module
        pybind11_add_module(engine pymodule.cpp)
        target_link_libraries(engine PRIVATE 5dchess_client)
        target_compile_definitions(engine PRIVATE 
            PROJECT_VERSION_STRING="${PROJECT_VERSION}"
        )
        
        # Fix Visual Studio output directories on Windows
        if(WIN32)
            # Get rid of the annoying Debug/Release folders generated by Visual Studio
            set_target_properties(engine PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<0:> 
                LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<0:>
                ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<0:>
            )
        endif(WIN32)
    endif()
    
    if(EMMODULE)
        message(STATUS ">>> Building Emscripten/WebAssembly module...")
        
        # Check if we're using Emscripten
        if(NOT EMSCRIPTEN)
            message(FATAL_ERROR "EMMODULE option requires compilation with Emscripten. Use: emcmake cmake ..")
        endif()
        
        # Create the WebAssembly module
        add_executable(engine_wasm emmodule.cpp)
        target_link_libraries(engine_wasm PRIVATE 5dchess_client)
        target_compile_definitions(engine_wasm PRIVATE 
            PROJECT_VERSION_STRING="${PROJECT_VERSION}"
        )
        
        # Enable WebAssembly native exception handling
        target_compile_options(engine_wasm PRIVATE -fwasm-exceptions)

        set(EMSCRIPTEN_LINK_FLAGS 
            "-lembind --bind -fwasm-exceptions \
            -sWASM=1 \
            -sALLOW_MEMORY_GROWTH=1 \
            -sMODULARIZE=1 \
            -sEXPORT_NAME=\"createModule\" \
            -sEXPORT_ES6=1 \
            -sEXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\"] \
            -sENVIRONMENT=web,worker,node \
            -sSTACK_SIZE=4194304"
        )

        #Release/Debug
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            set(EMSCRIPTEN_LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS} -O3")
        else()
            set(EMSCRIPTEN_LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS} \
                -sSTACK_OVERFLOW_CHECK=2 \
                -sASSERTIONS=1"
            )
        endif()

        # add_custom_command(TARGET engine_wasm POST_BUILD
        #     COMMAND ${CMAKE_COMMAND} -E copy_directory
        #     ${CMAKE_SOURCE_DIR}/ui
        #     $<TARGET_FILE_DIR:engine_wasm>
        # )

        add_custom_target(copy_files ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/ui" "${CMAKE_BINARY_DIR}/ui"
            COMMENT "Copying ui/ to build directory"
        )

        set_target_properties(engine_wasm PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ui/wasm"
            LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS}"
            OUTPUT_NAME "engine"
            SUFFIX ".js"
        )
    endif()
else()
    message(STATUS ">>> Building libraries as a cpp submodule...")
endif()
