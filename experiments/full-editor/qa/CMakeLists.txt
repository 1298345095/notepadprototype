find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(PkgConfig REQUIRED)
pkg_check_modules(QA_PANGO REQUIRED IMPORTED_TARGET pangocairo)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/completion_slice.inc"
  COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/extract.py" "${NOTEPAD4_SRC}" "${CMAKE_CURRENT_BINARY_DIR}/completion_slice.inc"
  DEPENDS extract.py "${NOTEPAD4_SRC}/src/EditAutoC.cpp" "${NOTEPAD4_SRC}/src/SciCall.h" "${NOTEPAD4_SRC}/src/EditLexers/stlJSON.cpp")
add_library(np4_study STATIC completion.cpp study.cpp "${CMAKE_CURRENT_BINARY_DIR}/completion_slice.inc")
target_compile_features(np4_study PUBLIC cxx_std_20)
target_include_directories(np4_study PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(np4_study PUBLIC np4_core PRIVATE PkgConfig::QA_PANGO)
