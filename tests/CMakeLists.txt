set(TEST_SOURCES
    parser/test_cmark_parser.cpp
)

foreach(TEST_FILE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_FILE})
    target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${TEST_NAME} PRIVATE Qt6::Test libcmark-gfm_static)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()
