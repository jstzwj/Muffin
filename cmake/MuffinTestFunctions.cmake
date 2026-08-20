# muffin_add_test — the single entry point for registering a Muffin test executable.
#
# Each call replaces the former 4-command boilerplate (add_executable + target_link_libraries +
# add_test + set_tests_properties), so the test section reads as one line per test and an
# add_executable / target_link_libraries pair can never drift apart — the old InputLazyBlockTest
# had them 94 lines apart because several executables were declared in a row and linked later.
#
#   muffin_add_test(NAME <target> SOURCE <cpp> LINK <lib>
#                   [EXTRA_SOURCES <src>...] [EXTRA_LINK <lib>...] [FIXTURE <rel-path>]
#                   [RESOURCE_LOCK] [DISABLED_ON APPLE ...])
#
# ENVIRONMENT_MODIFICATION is always applied from MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS (built in
# the top-level CMakeLists.txt). RESOURCE_LOCK is an explicit opt-in flag rather than something
# inferred from LINK: it depends on whether the test instantiates a QApplication, not on the
# library it links (six blocks/controller tests link MuffinUi but never create a GUI, so they
# correctly omit the lock and stay parallel).
#
# Every registration also appends to the build-freshness manifest — the
# AUTHORITATIVE target→sources+libs map MermaidBuildFreshnessTest consumes.
# Deriving a test's source by stripping the "Muffin" prefix from its exe name
# silently missed real tests whose name deliberately differs from the file
# (MuffinMermaidC4EdgeParityTest compiles MermaidC4GeometryOracleTest.cpp).
set(MUFFIN_FRESHNESS_MANIFEST "${CMAKE_BINARY_DIR}/mermaid-build-freshness-manifest.txt")
file(WRITE "${MUFFIN_FRESHNESS_MANIFEST}"
     "# Generated at configure time — consumed by MuffinBuildFreshnessTest\n")

# First-party link CLOSURE for the manifest: a test linking MuffinUi
# transitively links MuffinCore (PUBLIC). Recording only the direct LINK
# would let a Ui-listed test skip relinking after a Core-only rebuild
# without the freshness gate noticing — exactly the MSB8028 incident class.
function(muffin_freshness_link_closure out_var)
  set(accumulator "")
  set(expanded "")
  set(queue ${ARGN})
  while(NOT queue STREQUAL "")
    list(GET queue 0 item)
    list(REMOVE_AT queue 0)
    if(NOT item MATCHES "^Muffin")
      continue()
    endif()
    if(NOT item IN_LIST accumulator)
      list(APPEND accumulator "${item}")
    endif()
    if(item IN_LIST expanded OR NOT TARGET ${item})
      continue()
    endif()
    list(APPEND expanded "${item}")
    get_target_property(iface_links ${item} INTERFACE_LINK_LIBRARIES)
    get_target_property(direct_links ${item} LINK_LIBRARIES)
    foreach(dep IN LISTS iface_links direct_links)
      if(dep MATCHES "^Muffin" AND NOT dep IN_LIST accumulator)
        list(APPEND queue "${dep}")
      endif()
    endforeach()
  endwhile()
  list(REMOVE_DUPLICATES accumulator)
  set(${out_var} "${accumulator}" PARENT_SCOPE)
endfunction()

function(muffin_add_test)
  set(options RESOURCE_LOCK)
  set(oneValueArgs NAME SOURCE LINK FIXTURE)
  set(multiValueArgs EXTRA_SOURCES EXTRA_LINK DISABLED_ON)
  cmake_parse_arguments(MUFFIN_TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  add_executable(${MUFFIN_TEST_NAME}
      ${MUFFIN_TEST_SOURCE}
      ${MUFFIN_TEST_EXTRA_SOURCES})
  target_link_libraries(${MUFFIN_TEST_NAME} PRIVATE ${MUFFIN_TEST_LINK} ${MUFFIN_TEST_EXTRA_LINK})
  muffin_freshness_link_closure(freshness_links
      ${MUFFIN_TEST_LINK} ${MUFFIN_TEST_EXTRA_LINK})
  file(APPEND "${MUFFIN_FRESHNESS_MANIFEST}"
       "TEST\t${MUFFIN_TEST_NAME}\t${MUFFIN_TEST_SOURCE};${MUFFIN_TEST_EXTRA_SOURCES}\t${freshness_links}\n")

  if(MUFFIN_TEST_FIXTURE)
    add_test(NAME ${MUFFIN_TEST_NAME}
             COMMAND ${MUFFIN_TEST_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/${MUFFIN_TEST_FIXTURE}")
  else()
    add_test(NAME ${MUFFIN_TEST_NAME} COMMAND ${MUFFIN_TEST_NAME})
  endif()

  # Quote the environment list so its semicolons survive as a single property value.
  if(MUFFIN_TEST_RESOURCE_LOCK)
    set_tests_properties(${MUFFIN_TEST_NAME} PROPERTIES
        ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
        RESOURCE_LOCK MuffinQtGui)
  else()
    set_tests_properties(${MUFFIN_TEST_NAME} PROPERTIES
        ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}")
  endif()

  if(APPLE AND "APPLE" IN_LIST MUFFIN_TEST_DISABLED_ON)
    set_tests_properties(${MUFFIN_TEST_NAME} PROPERTIES DISABLED YES)
  endif()
endfunction()
