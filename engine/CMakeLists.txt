# Native Win32 control and built-in lexer catalogue from the pinned Notepad4 tree.
set(scintilla "${NOTEPAD4_ROOT}/scintilla")
if(NOT EXISTS "${scintilla}/win32/ScintillaWin.cxx")
  message(FATAL_ERROR "Set NOTEPAD4_ROOT to the pinned zufuliu/notepad4 checkout")
endif()
file(GLOB sources CONFIGURE_DEPENDS
  "${scintilla}/win32/*.cxx" "${scintilla}/src/*.cxx"
  "${scintilla}/lexlib/*.cxx" "${scintilla}/lexers/*.cxx")
add_library(np4_engine STATIC ${sources} app_context.cpp)
target_compile_features(np4_engine PRIVATE cxx_std_20)
set_target_properties(np4_engine PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_include_directories(np4_engine
  PUBLIC "${scintilla}/include"
  PRIVATE "${scintilla}/src" "${scintilla}/lexlib")
target_compile_definitions(np4_engine PRIVATE
  _WINDOWS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN STRICT_TYPED_ITEMIDS
  _WIN32_WINNT=0x0601 WINVER=0x0601 BOOST_REGEX_STANDALONE NO_CXX11_REGEX
  _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS)
target_compile_options(np4_engine PRIVATE /utf-8 /Zc:__cplusplus /bigobj)
target_link_libraries(np4_engine PRIVATE
  comctl32 imm32 gdi32 shlwapi shell32 ole32 oleaut32 uuid uxtheme)
