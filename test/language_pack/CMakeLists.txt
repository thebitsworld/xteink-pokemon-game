cmake_minimum_required(VERSION 3.16)
project(LanguagePack LANGUAGES CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(GENERATED_I18N "${CMAKE_CURRENT_BINARY_DIR}/i18n")
file(MAKE_DIRECTORY ${GENERATED_I18N})
# Firmware tables as shipped (English + Vietnamese built in) and the pack holding the rest.
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/gen_i18n.py
          ${REPO_ROOT}/lib/I18n/translations ${GENERATED_I18N} --builtin-langs vi
  WORKING_DIRECTORY ${REPO_ROOT}
  RESULT_VARIABLE I18N_RESULT
  OUTPUT_QUIET
)
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/build_language_pack.py
          --out ${CMAKE_CURRENT_BINARY_DIR}/language-pack.bin --builtin vi
  WORKING_DIRECTORY ${REPO_ROOT}
  RESULT_VARIABLE PACK_RESULT
  OUTPUT_QUIET
)
if(NOT I18N_RESULT EQUAL 0 OR NOT PACK_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to generate the language pack test data")
endif()
foreach(source I18n.h I18n.cpp LanguagePack.h LanguagePack.cpp)
  configure_file(${REPO_ROOT}/lib/I18n/${source} ${GENERATED_I18N}/${source} COPYONLY)
endforeach()
add_executable(language_pack LanguagePackTest.cpp
  ${GENERATED_I18N}/I18n.cpp ${GENERATED_I18N}/I18nStrings.cpp ${GENERATED_I18N}/LanguagePack.cpp)
target_include_directories(language_pack PRIVATE ${GENERATED_I18N})
target_compile_features(language_pack PRIVATE cxx_std_17)
target_compile_options(language_pack PRIVATE -UNDEBUG)
add_test(NAME language_pack COMMAND language_pack ${CMAKE_CURRENT_BINARY_DIR}/language-pack.bin)
