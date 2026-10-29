execute_process(
  COMMAND "${DOTNET_COMMAND}" build "${SOURCE_DIR}/tests/installer/IntegrationOptionsTest.csproj"
    -c Release --nologo -o "${BINARY_DIR}/installer-options-test"
    "-p:BaseIntermediateOutputPath=${BINARY_DIR}/installer-options-test/obj/"
  RESULT_VARIABLE build_result
)
if(build_result)
  message(FATAL_ERROR "Installer option test build failed: ${build_result}")
endif()
execute_process(
  COMMAND "${BINARY_DIR}/installer-options-test/IntegrationOptionsTest.exe"
  RESULT_VARIABLE test_result
)
if(test_result)
  message(FATAL_ERROR "Installer option test failed: ${test_result}")
endif()
