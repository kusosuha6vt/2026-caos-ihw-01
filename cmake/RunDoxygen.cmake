# Doxygen 1.18.0 on macOS ARM64 can intermittently crash in its comment scanner.
# CMakeLists enables retries only for that version/platform; normal failures
# (including WARN_AS_ERROR diagnostics) are never retried or ignored.
foreach(required IN ITEMS DOXYGEN CONFIG MAX_ATTEMPTS)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "RunDoxygen: missing ${required}")
    endif()
endforeach()
if(NOT MAX_ATTEMPTS MATCHES "^[1-5]$")
    message(FATAL_ERROR "RunDoxygen: MAX_ATTEMPTS must be 1..5")
endif()
if(NOT DEFINED DOXYGEN_TIMEOUT)
    set(DOXYGEN_TIMEOUT 30)
endif()

foreach(attempt RANGE 1 ${MAX_ATTEMPTS})
    execute_process(COMMAND "${DOXYGEN}" "${CONFIG}"
        TIMEOUT "${DOXYGEN_TIMEOUT}" RESULT_VARIABLE result)
    if("${result}" STREQUAL "0")
        return()
    endif()
    if(attempt LESS MAX_ATTEMPTS AND
       result MATCHES "^(Bus error|Segmentation fault|Process terminated due to timeout)$")
        message(WARNING
            "Doxygen failed (${result}); retrying ${attempt}/${MAX_ATTEMPTS} "
            "for the affected 1.18.0 tool. Documentation errors are not retried.")
    else()
        message(FATAL_ERROR
            "Doxygen failed (${result}) after ${attempt} attempt(s). "
            "Check diagnostics above; repeated crashes require a working Doxygen build.")
    endif()
endforeach()
