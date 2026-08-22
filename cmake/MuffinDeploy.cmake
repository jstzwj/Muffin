if(NOT DEFINED APP_FILE OR APP_FILE STREQUAL "")
  message(FATAL_ERROR "APP_FILE is required")
endif()

if(NOT DEFINED DIST_DIR OR DIST_DIR STREQUAL "")
  message(FATAL_ERROR "DIST_DIR is required")
endif()

file(REMOVE_RECURSE "${DIST_DIR}")
file(MAKE_DIRECTORY "${DIST_DIR}")

if(APPLE)
  # --- macOS: copy the entire .app bundle ---
  # When MACOSX_BUNDLE is set, APP_FILE points to Muffin.app/Contents/MacOS/Muffin.
  # Walk up to find the .app bundle root and copy it wholesale.
  get_filename_component(APP_MACOS_DIR "${APP_FILE}" DIRECTORY)       # Contents/MacOS
  get_filename_component(APP_CONTENTS_DIR "${APP_MACOS_DIR}" DIRECTORY) # Contents
  get_filename_component(APP_BUNDLE_DIR "${APP_CONTENTS_DIR}" DIRECTORY) # Muffin.app

  if(NOT EXISTS "${APP_BUNDLE_DIR}" OR NOT IS_DIRECTORY "${APP_BUNDLE_DIR}")
    message(FATAL_ERROR
      "Expected macOS app bundle at ${APP_BUNDLE_DIR} but it was not found. "
      "Ensure MACOSX_BUNDLE is set on the target.")
  endif()

  file(COPY "${APP_BUNDLE_DIR}" DESTINATION "${DIST_DIR}")
  message(STATUS "Muffin macOS dist written to ${DIST_DIR}/Muffin.app")

else()
  # --- Windows (and Linux): flat directory layout ---
  file(COPY "${APP_FILE}" DESTINATION "${DIST_DIR}")
  get_filename_component(APP_FILE_NAME "${APP_FILE}" NAME)

  set(runtime_dlls "")
  if(DEFINED RUNTIME_DLLS AND NOT RUNTIME_DLLS STREQUAL "")
    string(REPLACE "|" ";" runtime_dlls "${RUNTIME_DLLS}")
  endif()

  foreach(dll IN LISTS runtime_dlls)
    if(EXISTS "${dll}")
      file(COPY "${dll}" DESTINATION "${DIST_DIR}")
    endif()
  endforeach()

  set(plugin_dirs "")
  if(DEFINED QT_PLUGIN_DIRS AND NOT QT_PLUGIN_DIRS STREQUAL "")
    string(REPLACE "|" ";" plugin_dirs "${QT_PLUGIN_DIRS}")
  endif()

  foreach(dll IN LISTS runtime_dlls)
    get_filename_component(dll_name "${dll}" NAME)
    if(dll_name MATCHES "^Qt6.*\\.(dll|so)$")
      get_filename_component(qt_bin_dir "${dll}" DIRECTORY)
      get_filename_component(qt_root_dir "${qt_bin_dir}" DIRECTORY)
      foreach(plugin_subdir platforms styles imageformats tls networkinformation)
        if(EXISTS "${qt_root_dir}/plugins/${plugin_subdir}")
          list(APPEND plugin_dirs "${qt_root_dir}/plugins/${plugin_subdir}")
        endif()
      endforeach()
    endif()
  endforeach()

  if(UNIX AND NOT APPLE)
    # TARGET_RUNTIME_DLLS is empty on ELF platforms, so the loop above never
    # runs there. Resolve the executable's ELF dependency graph NOW and derive
    # the Qt plugin directories from the actual libQt6*.so locations instead
    # of trusting only the configure-time QT_PLUGIN_DIRS bookkeeping. A silent
    # miss here ships a dist bundle without platform plugins, which only fails
    # at the consumer's first QGuiApplication (v0.6.0 Linux release).
    #
    # GET_RUNTIME_DEPENDENCIES' implicit search (rpath/ldconfig/env) proved
    # unreliable on CI runners -- the v0.6.0 rerun left the direct Qt6
    # dependencies "unresolved" with the Conan cache present. Feed it the
    # loader's own search list explicitly: the executable's DT_RPATH/DT_RUNPATH
    # entries plus the lib dirs implied by any configure-time plugin dir.
    find_program(PATCHELF_EXECUTABLE NAMES patchelf REQUIRED)
    set(exe_search_dirs "")
    execute_process(
      COMMAND "${PATCHELF_EXECUTABLE}" --print-rpath "${DIST_DIR}/${APP_FILE_NAME}"
      OUTPUT_VARIABLE exe_rpath_raw
      RESULT_VARIABLE exe_rpath_result
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(exe_rpath_result EQUAL 0 AND NOT exe_rpath_raw STREQUAL "")
      string(REPLACE ":" ";" exe_rpath_dirs "${exe_rpath_raw}")
      foreach(rpath_dir IN LISTS exe_rpath_dirs)
        if(IS_ABSOLUTE "${rpath_dir}" AND IS_DIRECTORY "${rpath_dir}")
          list(APPEND exe_search_dirs "${rpath_dir}")
        endif()
      endforeach()
    endif()
    foreach(plugin_dir IN LISTS plugin_dirs)
      get_filename_component(qt_plugins_root "${plugin_dir}" DIRECTORY)
      get_filename_component(qt_root_dir "${qt_plugins_root}" DIRECTORY)
      foreach(qt_lib_subdir lib bin)
        if(IS_DIRECTORY "${qt_root_dir}/${qt_lib_subdir}")
          list(APPEND exe_search_dirs "${qt_root_dir}/${qt_lib_subdir}")
        endif()
      endforeach()
    endforeach()
    if(DEFINED ENV{LD_LIBRARY_PATH} AND NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
      # The conan build preset exports LD_LIBRARY_PATH with every Conan lib
      # dir; the deploy script inherits it. Trust it as a search source too.
      string(REPLACE ":" ";" ld_library_path_dirs "$ENV{LD_LIBRARY_PATH}")
      foreach(ld_dir IN LISTS ld_library_path_dirs)
        if(IS_ABSOLUTE "${ld_dir}" AND IS_DIRECTORY "${ld_dir}")
          list(APPEND exe_search_dirs "${ld_dir}")
        endif()
      endforeach()
    endif()
    if(exe_search_dirs)
      list(REMOVE_DUPLICATES exe_search_dirs)
    endif()
    file(GET_RUNTIME_DEPENDENCIES
      EXECUTABLES "${DIST_DIR}/${APP_FILE_NAME}"
      DIRECTORIES ${exe_search_dirs}
      RESOLVED_DEPENDENCIES_VAR exe_resolved_dependencies
      UNRESOLVED_DEPENDENCIES_VAR exe_unresolved_dependencies
      POST_EXCLUDE_REGEXES "^/lib/" "^/lib64/" "^/usr/lib/" "^/usr/lib64/"
    )
    if(exe_unresolved_dependencies)
      message(FATAL_ERROR "Unresolved Linux runtime dependencies: ${exe_unresolved_dependencies} (searched: ${exe_search_dirs}; rpath: '${exe_rpath_raw}')")
    endif()
    foreach(dependency IN LISTS exe_resolved_dependencies)
      get_filename_component(dependency_name "${dependency}" NAME)
      if(dependency_name MATCHES "^libQt6.*\\.so")
        get_filename_component(qt_lib_dir "${dependency}" DIRECTORY)
        get_filename_component(qt_root_dir "${qt_lib_dir}" DIRECTORY)
        foreach(plugin_subdir platforms styles imageformats tls networkinformation)
          if(EXISTS "${qt_root_dir}/plugins/${plugin_subdir}")
            list(APPEND plugin_dirs "${qt_root_dir}/plugins/${plugin_subdir}")
          endif()
        endforeach()
      endif()
    endforeach()
  endif()

  if(plugin_dirs)
    list(REMOVE_DUPLICATES plugin_dirs)
  endif()

  foreach(plugin_dir IN LISTS plugin_dirs)
    if(EXISTS "${plugin_dir}")
      get_filename_component(plugin_name "${plugin_dir}" NAME)
      # Plugins are .dll on Windows and .so on Linux/macOS; glob both so the
      # dist bundle actually carries the platform plugins it needs at runtime.
      file(GLOB plugin_files "${plugin_dir}/*.dll" "${plugin_dir}/*.so")
      if(plugin_files)
        file(MAKE_DIRECTORY "${DIST_DIR}/${plugin_name}")
        file(COPY ${plugin_files} DESTINATION "${DIST_DIR}/${plugin_name}")
      endif()
    endif()
  endforeach()

  if(UNIX AND NOT APPLE)
    # TARGET_RUNTIME_DLLS is empty on ELF platforms. Resolve the executable and
    # plugin dependency graph directly, copy only non-system libraries, then
    # make every binary find the flat bundle without Conan's cache paths.
    file(GLOB_RECURSE bundled_plugins "${DIST_DIR}/*.so")
    file(GET_RUNTIME_DEPENDENCIES
      EXECUTABLES "${DIST_DIR}/${APP_FILE_NAME}"
      LIBRARIES ${bundled_plugins}
      DIRECTORIES ${exe_search_dirs}
      RESOLVED_DEPENDENCIES_VAR resolved_dependencies
      UNRESOLVED_DEPENDENCIES_VAR unresolved_dependencies
      CONFLICTING_DEPENDENCIES_PREFIX conflicting_dependencies
      POST_EXCLUDE_REGEXES "^/lib/" "^/lib64/" "^/usr/lib/" "^/usr/lib64/"
    )
    if(unresolved_dependencies)
      message(FATAL_ERROR "Unresolved Linux runtime dependencies: ${unresolved_dependencies}")
    endif()
    if(conflicting_dependencies_FILENAMES)
      message(FATAL_ERROR
        "Conflicting Linux runtime dependencies: ${conflicting_dependencies_FILENAMES}")
    endif()
    foreach(dependency IN LISTS resolved_dependencies)
      # Runtime dependency resolution commonly returns a versioned symlink.
      # Copy the complete relative link chain so the DT_NEEDED name resolves.
      file(COPY "${dependency}" DESTINATION "${DIST_DIR}" FOLLOW_SYMLINK_CHAIN)
      get_filename_component(dependency_name "${dependency}" NAME)
      list(APPEND bundled_dependencies "${DIST_DIR}/${dependency_name}")
    endforeach()
    list(REMOVE_DUPLICATES bundled_dependencies)

    find_program(PATCHELF_EXECUTABLE NAMES patchelf REQUIRED)
    function(muffin_set_bundle_rpath binary new_rpath)
      execute_process(
        COMMAND "${PATCHELF_EXECUTABLE}" --set-rpath "${new_rpath}" "${binary}"
        RESULT_VARIABLE patchelf_result
        ERROR_VARIABLE patchelf_error
      )
      if(NOT patchelf_result EQUAL 0)
        message(FATAL_ERROR "Could not set RPATH on ${binary}: ${patchelf_error}")
      endif()
    endfunction()

    muffin_set_bundle_rpath("${DIST_DIR}/${APP_FILE_NAME}" "$ORIGIN")
    foreach(plugin IN LISTS bundled_plugins)
      muffin_set_bundle_rpath("${plugin}" "$ORIGIN/..")
    endforeach()
    foreach(dependency IN LISTS bundled_dependencies)
      muffin_set_bundle_rpath("${dependency}" "$ORIGIN")
    endforeach()
  endif()

  # The platform plugins are load-or-die for any Qt GUI app (the qt.conf below
  # points Plugins at this directory). Fail the dist build right here, with
  # the derived plugin dirs in the message, instead of shipping a bundle that
  # aborts on the consumer's machine.
  file(GLOB platform_plugins "${DIST_DIR}/platforms/*.dll" "${DIST_DIR}/platforms/*.so")
  if(NOT platform_plugins)
    file(GLOB dist_entries RELATIVE "${DIST_DIR}" "${DIST_DIR}/*")
    message(FATAL_ERROR
      "No Qt platform plugins were bundled into ${DIST_DIR}/platforms. "
      "Derived plugin dirs: ${plugin_dirs}. "
      "Configure-time QT_PLUGIN_DIRS: '${QT_PLUGIN_DIRS}'. "
      "Dist entries: ${dist_entries}. "
      "The Qt package layout or the plugin-dir derivation has changed.")
  endif()

  file(WRITE "${DIST_DIR}/qt.conf" "[Paths]\nPlugins = .\n")
  message(STATUS "Muffin dist written to ${DIST_DIR}")
endif()
