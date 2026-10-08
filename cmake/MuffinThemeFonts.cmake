# Author only webfonts; native font containers and their QRC live in the build tree.
execute_process(
  COMMAND "${Python3_EXECUTABLE}" -c "from fontTools.ttLib import TTFont; import brotli"
  RESULT_VARIABLE _muffin_font_tools_result
  ERROR_VARIABLE _muffin_font_tools_error
)
if(NOT _muffin_font_tools_result EQUAL 0)
  message(FATAL_ERROR
    "Theme font conversion requires FontTools and Brotli for ${Python3_EXECUTABLE}. "
    "Run: \"${Python3_EXECUTABLE}\" -m pip install -r \"${CMAKE_CURRENT_SOURCE_DIR}/requirements-build.txt\"\n"
    "${_muffin_font_tools_error}")
endif()

file(GLOB_RECURSE MUFFIN_THEME_WEBFONTS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/themes/*.woff"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/themes/*.woff2")
list(SORT MUFFIN_THEME_WEBFONTS)
set(MUFFIN_THEME_NATIVE_FONTS)
set(_muffin_font_entries "")
foreach(_source IN LISTS MUFFIN_THEME_WEBFONTS)
  file(RELATIVE_PATH _alias "${CMAKE_CURRENT_SOURCE_DIR}/resources/themes" "${_source}")
  string(REGEX REPLACE "\\.woff2?$" ".ttf" _native_relative "${_alias}")
  set(_native "${CMAKE_CURRENT_BINARY_DIR}/theme-fonts/${_native_relative}")
  if(_native IN_LIST MUFFIN_THEME_NATIVE_FONTS)
    message(FATAL_ERROR "Theme fonts share a native output: ${_native_relative}")
  endif()
  add_custom_command(
    OUTPUT "${_native}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/prepare_theme_fonts.py"
      --input "${_source}" --output "${_native}"
    DEPENDS "${_source}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/prepare_theme_fonts.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/requirements-build.txt"
    COMMENT "Convert theme font ${_alias}"
    VERBATIM)
  list(APPEND MUFFIN_THEME_NATIVE_FONTS "${_native}")
  # QRC aliases preserve authored CSS URLs even though their bytes are native SFNT.
  string(REPLACE "&" "&amp;" _native_xml "${_native}")
  string(REPLACE "&" "&amp;" _alias_xml "${_alias}")
  string(APPEND _muffin_font_entries "    <file alias=\"${_alias_xml}\">${_native_xml}</file>\n")
endforeach()
add_custom_target(muffin_theme_fonts DEPENDS ${MUFFIN_THEME_NATIVE_FONTS})

set(MUFFIN_THEMES_QRC "${CMAKE_CURRENT_BINARY_DIR}/themes.qrc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/themes.qrc")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/themes.qrc" _muffin_theme_qrc)
string(REPLACE "&" "&amp;" _muffin_resources_xml "${CMAKE_CURRENT_SOURCE_DIR}/resources/")
string(REPLACE "../resources/" "${_muffin_resources_xml}" _muffin_theme_qrc "${_muffin_theme_qrc}")
string(REPLACE "  </qresource>" "${_muffin_font_entries}  </qresource>" _muffin_theme_qrc "${_muffin_theme_qrc}")
file(CONFIGURE OUTPUT "${MUFFIN_THEMES_QRC}" CONTENT "${_muffin_theme_qrc}" @ONLY)

function(muffin_use_theme_fonts target)
  add_dependencies(${target} muffin_theme_fonts)
  set_property(TARGET ${target} APPEND PROPERTY AUTOGEN_TARGET_DEPENDS muffin_theme_fonts)
endfunction()
