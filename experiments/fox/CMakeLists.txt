cmake_minimum_required(VERSION 3.25)
project(fox_native_probe LANGUAGES CXX)
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Configure with x64 MSVC")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
get_filename_component(repo "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(NOTEPAD4_ROOT "${repo}/deps/notepad4" CACHE PATH "Pinned Notepad4 checkout")
set(FOX_ROOT "${repo}/deps/fox-1.6.59" CACHE PATH "FOX 1.6.59 source tree")
add_subdirectory("${repo}/engine" engine)

# The original VC6 static-library project lists all 305 src/*.cpp files. The
# modern UCRT already supplies the three functions that older MSVC lacked.
file(GLOB fox_sources CONFIGURE_DEPENDS "${FOX_ROOT}/src/*.cpp")
list(LENGTH fox_sources count)
if(NOT count EQUAL 305)
  message(FATAL_ERROR "Expected the FOX 1.6.59 source distribution")
endif()
add_library(fox STATIC ${fox_sources})
target_include_directories(fox PUBLIC "${FOX_ROOT}/include")
target_compile_definitions(fox PUBLIC WIN32 _WINDOWS UNICODE _UNICODE PRIVATE
  HAVE_GL_H HAVE_GLU_H HAVE_VSSCANF HAVE_STRTOLL HAVE_STRTOULL
  _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
target_compile_options(fox PRIVATE /Gy /Gw /W1)
target_link_libraries(fox PUBLIC comctl32 ws2_32 kernel32 user32 gdi32 advapi32 shell32 mpr
  opengl32 glu32 comdlg32 winspool imm32 ole32)
add_executable(np4-fox WIN32 fox_host.cpp)
target_compile_features(np4-fox PRIVATE cxx_std_20)
target_compile_options(np4-fox PRIVATE /utf-8 /Zc:__cplusplus)
target_compile_definitions(np4-fox PRIVATE NOMINMAX _WIN32_WINNT=0x0601 WINVER=0x0601)
target_include_directories(np4-fox PRIVATE "${repo}/src")
target_link_libraries(np4-fox PRIVATE np4_engine fox)
set_target_properties(np4-fox PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${repo}/out/fox")
