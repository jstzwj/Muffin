# Use ICU's published common-format data package, without interpreting object
# files or running target tools. The same data works on x64 and ARM64.
# Conan still owns the ICU libraries; require the exact matching data version.
function(muffin_externalize_icu_data target)
  set(icu_data_version "74.2")
  foreach(config RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
    if(NOT DEFINED icu_LIB_DIRS_${config})
      continue()
    endif()
    if(NOT icu_LIBRARY_TYPE_${config} STREQUAL "STATIC")
      message(FATAL_ERROR "Muffin's external ICU data requires icu/*:shared=False")
    endif()
    find_file(icu_version_header_${config} NAMES unicode/uvernum.h
      PATHS ${icu_INCLUDE_DIRS_${config}} NO_DEFAULT_PATH NO_CACHE REQUIRED)
    file(STRINGS "${icu_version_header_${config}}" version_line REGEX "^#define U_ICU_VERSION ")
    string(REGEX REPLACE "^#define U_ICU_VERSION +\"([^\"]+)\".*" "\\1" version "${version_line}")
    if(NOT version STREQUAL icu_data_version)
      message(FATAL_ERROR "ICU ${version} requires its matching official data package; update MuffinIcuData.cmake")
    endif()
    list(APPEND icu_configurations ${config})
  endforeach()
  if(NOT icu_configurations)
    message(FATAL_ERROR "Could not locate the Conan ICU configuration")
  endif()

  if(CMAKE_CXX_BYTE_ORDER STREQUAL "BIG_ENDIAN")
    set(order b)
    set(archive_sha256 "42a12ebfb1a82f80bb0005d9b6e018382ccaa2462f0d086a8c69ae736fdded3e")
    set(data_sha256 "8da022abc2cf7817a79cc046eaa7547b73ade60418f1e93c518e7c4614d186df")
  else()
    set(order l)
    set(archive_sha256 "2acdb1b982228040963d183b2dd9d321252c613e0f4db213d4bbc10417cde569")
    set(data_sha256 "dc778b9ffe18ed319ad3fb70754f80e51cf7b6dbfff38fc0c0a5f27bb5463dad")
  endif()
  include(FetchContent)
  FetchContent_Declare(muffin_icu_data
    URL "https://github.com/unicode-org/icu/releases/download/release-74-2/icu4c-74_2-data-bin-${order}.zip"
    URL_HASH "SHA256=${archive_sha256}"
    TLS_VERIFY TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(muffin_icu_data)

  set(MUFFIN_ICU_DATA_FILE "icudt74${order}.dat")
  set(data_file "${muffin_icu_data_SOURCE_DIR}/${MUFFIN_ICU_DATA_FILE}")
  file(SIZE "${data_file}" MUFFIN_ICU_DATA_BYTES)
  file(SHA256 "${data_file}" MUFFIN_ICU_DATA_SHA256)
  # Also validate source-directory overrides and previously populated caches.
  if(NOT MUFFIN_ICU_DATA_SHA256 STREQUAL data_sha256)
    message(FATAL_ERROR "ICU data differs from the official ${icu_data_version} ${order}-endian package: ${data_file}")
  endif()
  set(directory "${CMAKE_CURRENT_BINARY_DIR}/generated/icu")
  configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/IcuDataConfig.h.in"
    "${directory}/IcuDataConfig.h" @ONLY)
  target_include_directories(${target} PRIVATE "${directory}")
  foreach(config IN LISTS icu_configurations)
    set(MUFFIN_ICU_DATA_${config} "${data_file}" PARENT_SCOPE)
  endforeach()
  add_custom_target(muffin-icu-data
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/resources/icu"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${data_file}"
      "$<TARGET_FILE_DIR:${target}>/resources/icu/"
    DEPENDS "${data_file}"
    VERBATIM)
  add_dependencies(${target} muffin-icu-data)
  add_library(muffin-icu-stub OBJECT third_party/icu-stubdata/stubdata.cpp)
  set_target_properties(muffin-icu-stub PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
  target_link_libraries(muffin-icu-stub PRIVATE ICU::data)
  # In STATIC builds the stub must reach the final link as a regular object,
  # rather than sit unreferenced in an archive scanned before ICU::uc.
  if(MUFFIN_LIB_TYPE STREQUAL "STATIC")
    target_sources(${target} INTERFACE $<TARGET_OBJECTS:muffin-icu-stub>)
  else()
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:muffin-icu-stub>)
  endif()
  target_sources(${target} PRIVATE src/unicode/IcuRuntime.cpp)
endfunction()
