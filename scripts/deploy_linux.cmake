# deploy_linux.cmake — Linux deployment
# Sets rpath and collects shared libraries for a portable distribution.

find_program(PATCHELF patchelf)
find_program(CHRPATH chrpath)

# Set rpath to look for libs next to the executable
if(PATCHELF)
    execute_process(
        COMMAND "${PATCHELF}" --set-rpath "$ORIGIN/lib" "${DEPLOY_DIR}/muffin"
        RESULT_VARIABLE RESULT
    )
    if(RESULT EQUAL 0)
        message(STATUS "  Set rpath to \$ORIGIN/lib (patchelf)")
    endif()
elseif(CHRPATH)
    execute_process(
        COMMAND "${CHRPATH}" -r "\$ORIGIN/lib" "${DEPLOY_DIR}/muffin"
        RESULT_VARIABLE RESULT
    )
    if(RESULT EQUAL 0)
        message(STATUS "  Set rpath to \$ORIGIN/lib (chrpath)")
    endif()
else()
    message(WARNING "  Neither patchelf nor chrpath found. "
        "Set LD_LIBRARY_PATH when running, or install patchelf.")
endif()

# Create lib directory
file(MAKE_DIRECTORY "${DEPLOY_DIR}/lib")

# Collect shared libraries from build deps
set(LIB_SEARCH_DIRS
    "${CMAKE_BINARY_DIR}/_deps/cmark-gfm-build/src"
    "${CMAKE_BINARY_DIR}/generators/lib"
)

foreach(DIR ${LIB_SEARCH_DIRS})
    if(EXISTS "${DIR}")
        file(GLOB LIBS "${DIR}/*.so*")
        foreach(LIB ${LIBS})
            get_filename_component(LIB_NAME "${LIB}" NAME)
            file(COPY "${LIB}" DESTINATION "${DEPLOY_DIR}/lib" FOLLOW_SYMLINK_CHAIN)
            message(STATUS "  Copied: ${LIB_NAME}")
        endforeach()
    endif()
endforeach()

# Use ldd to collect all required shared libraries
find_program(LDD ldd)
if(LDD AND EXISTS "${DEPLOY_DIR}/muffin")
    execute_process(
        COMMAND "${LDD}" "${DEPLOY_DIR}/muffin"
        OUTPUT_VARIABLE LDD_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REPLACE "\n" ";" LDD_LINES "${LDD_OUTPUT}")
    foreach(LINE ${LDD_LINES})
        if(LINE MATCHES "=> (/[^ ]+\\.so[^ ]*)")
            string(REGEX REPLACE ".*=> (/[^ ]+\\.so[^ ]*).*" "\\1" LIB_PATH "${LINE}")
            # Skip system libs (glibc, ld-linux, etc.)
            if(NOT LIB_PATH MATCHES "^(/lib/x86_64-linux-gnu/|^/lib64/|^/usr/lib)")
                if(EXISTS "${LIB_PATH}")
                    get_filename_component(LIB_NAME "${LIB_PATH}" NAME)
                    file(COPY "${LIB_PATH}" DESTINATION "${DEPLOY_DIR}/lib" FOLLOW_SYMLINK_CHAIN)
                    message(STATUS "  Bundled: ${LIB_NAME}")
                endif()
            endif()
        endif()
    endforeach()
endif()
