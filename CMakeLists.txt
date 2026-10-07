cmake_minimum_required(VERSION 3.20)
# Export the SDK explicitly so non-Apple tools can replay Apple compiler commands.
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_SYSROOT)
    set(CMAKE_OSX_SYSROOT macosx CACHE STRING "Apple platform SDK" FORCE)
endif()

project(gallery VERSION 1.0 LANGUAGES C)

# Available with the Ninja and Makefile generators; clangd finds build/ directly.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GALLERY_SOURCES
    src/main.c
    src/config.c
    src/simulation.c
    src/strategy.c
    src/invariants.c
    src/output.c
    src/signals.c
)

function(configure_gallery target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_compile_definitions(${target} PRIVATE _POSIX_C_SOURCE=200809L)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
            -Wmissing-prototypes -Wstrict-prototypes -Werror
        )
    endif()
endfunction()

add_executable(gallery ${GALLERY_SOURCES})
configure_gallery(gallery)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_executable(gallery-sanitize EXCLUDE_FROM_ALL ${GALLERY_SOURCES})
    configure_gallery(gallery-sanitize)
    # Editors/lint use the normal build, without duplicate sanitizer commands.
    set_target_properties(gallery-sanitize PROPERTIES EXPORT_COMPILE_COMMANDS OFF)
    target_compile_options(gallery-sanitize PRIVATE
        -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(gallery-sanitize PRIVATE -fsanitize=address,undefined)
endif()

include(CTest)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    if(BUILD_TESTING)
        add_test(NAME gallery-suite COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/run.py"
            --binary $<TARGET_FILE:gallery> --jobs 4)
        set_tests_properties(gallery-suite PROPERTIES TIMEOUT 180)
    endif()
    add_custom_target(check
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/run.py"
            --binary $<TARGET_FILE:gallery> --jobs 4
        DEPENDS gallery USES_TERMINAL VERBATIM)
    if(TARGET gallery-sanitize)
        add_custom_target(sanitize
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/run.py"
                --binary $<TARGET_FILE:gallery-sanitize> --jobs 4
            DEPENDS gallery-sanitize USES_TERMINAL VERBATIM)
    endif()
    add_custom_target(experiments
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/experiments.py"
            --binary $<TARGET_FILE:gallery> --jobs 4
            --output "${PROJECT_SOURCE_DIR}/output/experiments"
        DEPENDS gallery USES_TERMINAL VERBATIM)
else()
    message(STATUS "Python3 not found: test/check/sanitize/experiments unavailable")
endif()

find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
if(CLANG_TIDY_EXECUTABLE)
    set(GALLERY_TIDY_COMMANDS)
    foreach(source IN LISTS GALLERY_SOURCES)
        list(APPEND GALLERY_TIDY_COMMANDS
            COMMAND "${CLANG_TIDY_EXECUTABLE}" -p "${PROJECT_BINARY_DIR}"
                "${PROJECT_SOURCE_DIR}/${source}")
    endforeach()
    add_custom_target(clang-tidy ${GALLERY_TIDY_COMMANDS}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" USES_TERMINAL VERBATIM)
else()
    message(STATUS "clang-tidy not found: clang-tidy target unavailable")
endif()

find_program(TYPST_EXECUTABLE NAMES typst)
if(TYPST_EXECUTABLE)
    add_custom_target(report
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${PROJECT_SOURCE_DIR}/output/pdf"
        COMMAND "${TYPST_EXECUTABLE}" compile --root "${PROJECT_SOURCE_DIR}"
            "${PROJECT_SOURCE_DIR}/report/main.typ"
            "${PROJECT_SOURCE_DIR}/output/pdf/report.pdf"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        USES_TERMINAL VERBATIM)
else()
    message(STATUS "Typst not found: report target unavailable")
endif()

# Documentation is opt-in and does not add runtime/build dependencies.
find_package(Doxygen QUIET COMPONENTS doxygen)
if(Doxygen_FOUND)
    configure_file(docs/Doxyfile.in "${PROJECT_BINARY_DIR}/Doxyfile" @ONLY)
    set(GALLERY_DOXYGEN_ATTEMPTS 1)
    if(CMAKE_HOST_APPLE AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64" AND
       DOXYGEN_VERSION VERSION_EQUAL "1.18.0")
        set(GALLERY_DOXYGEN_ATTEMPTS 3)
    endif()
    add_custom_target(docs
        COMMAND "${CMAKE_COMMAND}"
            "-DDOXYGEN=$<TARGET_FILE:Doxygen::doxygen>"
            "-DCONFIG=${PROJECT_BINARY_DIR}/Doxyfile"
            "-DMAX_ATTEMPTS=${GALLERY_DOXYGEN_ATTEMPTS}"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunDoxygen.cmake"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Generating API documentation in ${PROJECT_BINARY_DIR}/docs/html"
        USES_TERMINAL VERBATIM)
    add_custom_target(doxygen DEPENDS docs)
else()
    message(STATUS "Doxygen not found: docs target unavailable")
endif()
