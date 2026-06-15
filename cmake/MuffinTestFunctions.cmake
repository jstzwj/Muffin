# muffin_add_test — the single entry point for registering a Muffin test executable.
#
# Each call replaces the former 4-command boilerplate (add_executable + target_link_libraries +
# add_test + set_tests_properties), so the test section reads as one line per test and an
# add_executable / target_link_libraries pair can never drift apart — the old InputLazyBlockTest
# had them 94 lines apart because several executables were declared in a row and linked later.
#
#   muffin_add_test(NAME <target> SOURCE <cpp> LINK <lib>
#                   [EXTRA_SOURCES <src>...] [FIXTURE <rel-path>]
#                   [RESOURCE_LOCK] [DISABLED_ON APPLE ...])
#
# ENVIRONMENT_MODIFICATION is always applied from MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS (built in
# the top-level CMakeLists.txt). RESOURCE_LOCK is an explicit opt-in flag rather than something
# inferred from LINK: it depends on whether the test instantiates a QApplication, not on the
# library it links (six blocks/controller tests link MuffinUi but never create a GUI, so they
# correctly omit the lock and stay parallel).
function(muffin_add_test)
  set(options RESOURCE_LOCK)
  set(oneValueArgs NAME SOURCE LINK FIXTURE)
  set(multiValueArgs EXTRA_SOURCES DISABLED_ON)
  cmake_parse_arguments(MUFFIN_TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  add_executable(${MUFFIN_TEST_NAME}
      ${MUFFIN_TEST_SOURCE}
      ${MUFFIN_TEST_EXTRA_SOURCES})
  target_link_libraries(${MUFFIN_TEST_NAME} PRIVATE ${MUFFIN_TEST_LINK})

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
