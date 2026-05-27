set(TEST_SOURCES
    parser/test_cmark_parser.cpp
    renderer/test_render_source_map.cpp
    editor/test_markdown_patch.cpp
)

foreach(TEST_FILE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_FILE})
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMARK_GFM_INCLUDE_DIRS})
    target_link_libraries(${TEST_NAME} PRIVATE Qt6::Test libcmark-gfm_static libcmark-gfm-extensions_static)
    if(TEST_FILE MATCHES "renderer/test_render_source_map.cpp")
        target_sources(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp)
    endif()
    if(TEST_FILE MATCHES "editor/test_markdown_patch.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/MarkdownPatch.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp)
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()
