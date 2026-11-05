# Modern Finder previews are executable .appex bundles, not legacy .qlgenerator
# plug-ins. The preview links only the existing static Markdown parser + AppKit.
enable_language(OBJCXX)
find_library(MUFFIN_APPKIT_FRAMEWORK AppKit REQUIRED)
find_library(MUFFIN_QUICKLOOK_FRAMEWORK QuickLookUI REQUIRED)
find_program(MUFFIN_CODESIGN_EXECUTABLE codesign REQUIRED)

add_library(MuffinQuickLookText STATIC src/quicklook/MarkdownPreview.mm)
set_target_properties(MuffinQuickLookText PROPERTIES
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES)
target_compile_options(MuffinQuickLookText PRIVATE -fobjc-arc -fapplication-extension)
target_include_directories(MuffinQuickLookText PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/src"
          "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/extensions"
          "${CMAKE_CURRENT_BINARY_DIR}/third_party/cmark-gfm/src")
target_link_libraries(MuffinQuickLookText PUBLIC "${MUFFIN_APPKIT_FRAMEWORK}"
  PRIVATE libcmark-gfm-extensions_static libcmark-gfm_static)

add_executable(MuffinQuickLook MACOSX_BUNDLE src/quicklook/PreviewController.mm)
set_target_properties(MuffinQuickLook PROPERTIES
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES
  BUNDLE_EXTENSION appex
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickLook-Info.plist.in"
  XCODE_ATTRIBUTE_APPLICATION_EXTENSION_API_ONLY YES
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
target_compile_options(MuffinQuickLook PRIVATE -fobjc-arc -fapplication-extension)
target_link_options(MuffinQuickLook PRIVATE -fapplication-extension "LINKER:-e,_NSExtensionMain")
target_link_libraries(MuffinQuickLook PRIVATE MuffinQuickLookText "${MUFFIN_QUICKLOOK_FRAMEWORK}")
add_custom_command(TARGET MuffinQuickLook POST_BUILD
  COMMAND "${MUFFIN_CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none
    --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickLook.entitlements"
    "$<TARGET_BUNDLE_DIR:MuffinQuickLook>"
  VERBATIM)

# This target also runs when only the extension changes and the host executable
# does not need relinking. Both the default build and dist embed the fresh copy.
add_custom_target(muffin-quicklook-embed ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:Muffin>/PlugIns"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:MuffinQuickLook>"
    "$<TARGET_BUNDLE_CONTENT_DIR:Muffin>/PlugIns/MuffinQuickLook.appex"
  DEPENDS Muffin MuffinQuickLook
  VERBATIM)

function(muffin_add_quicklook_tests)
  add_executable(MuffinQuickLookTest tests/quicklook/MarkdownPreviewTest.mm src/quicklook/PreviewController.mm)
  set_target_properties(MuffinQuickLookTest PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES)
  target_compile_options(MuffinQuickLookTest PRIVATE -fobjc-arc)
  target_link_libraries(MuffinQuickLookTest PRIVATE MuffinQuickLookText "${MUFFIN_QUICKLOOK_FRAMEWORK}")
  add_test(NAME MuffinQuickLookTest COMMAND MuffinQuickLookTest)
  set_tests_properties(MuffinQuickLookTest PROPERTIES RESOURCE_LOCK MuffinQtGui)
endfunction()
