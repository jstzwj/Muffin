set(TEST_SOURCES
    test_parser.cpp
    test_renderer.cpp
)

foreach(test_file ${TEST_SOURCES})
    get_filename_component(test_name ${test_file} NAME_WE)
    add_executable(${test_name} ${test_file})
    target_link_libraries(${test_name} PRIVATE
        muffin_lib
        Qt6::Test
    )
    if(MSVC)
        target_compile_options(${test_name} PRIVATE /Zc:__cplusplus /permissive- /utf-8)
    endif()
    # Same Conan Qt Debug include fix
    if(_MUFFIN_QT_INCLUDE_DIR AND IS_DIRECTORY "${_MUFFIN_QT_INCLUDE_DIR}")
        target_include_directories(${test_name} PRIVATE
            "${_MUFFIN_QT_INCLUDE_DIR}"
            "${_MUFFIN_QT_INCLUDE_DIR}/QtCore"
            "${_MUFFIN_QT_INCLUDE_DIR}/QtGui"
            "${_MUFFIN_QT_INCLUDE_DIR}/QtWidgets"
            "${_MUFFIN_QT_INCLUDE_DIR}/QtTest"
        )
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
